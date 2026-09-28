#include "audiodecoder.h"

#include <QFileInfo>
#include <QtGlobal>
#include <cstdlib>

#if HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#endif

namespace {

constexpr int OutputChannels = 2;

QVector<qreal> computePeaks(const AudioBuffer& audio)
{
    QVector<qreal> peaks;
    const qint64 frames = audio.frames();
    if (frames == 0) {
        return peaks;
    }
    const qint64 window = qMax<qint64>(1, audio.sampleRate / AudioDecoder::PeaksPerSecond);
    peaks.reserve(int(frames / window) + 1);

    const qint16* s = audio.samples.constData();
    int maxPeak = 0;
    for (qint64 f = 0; f < frames; f += window) {
        const qint64 endFrame = qMin(frames, f + window);
        int peak = 0;
        for (qint64 i = f * audio.channels; i < endFrame * audio.channels; ++i) {
            peak = qMax(peak, std::abs(int(s[i])));
        }
        peaks.append(peak);
        maxPeak = qMax(maxPeak, peak);
    }
    if (maxPeak > 0) {
        for (qreal& p : peaks) {
            p /= maxPeak;
        }
    }
    return peaks;
}

#if HAVE_FFMPEG
// Owns every FFmpeg object for one decode and frees them on any exit path
struct DecodeContext {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;

    ~DecodeContext()
    {
        av_packet_free(&packet);
        av_frame_free(&frame);
        swr_free(&swr);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};

// Resamples one frame (or flushes the resampler when frame is null) and appends the result
bool appendConverted(SwrContext* swr, const AVFrame* frame, QVector<qint16>& out)
{
    const int inSamples = frame ? frame->nb_samples : 0;
    const int capacity = swr_get_out_samples(swr, inSamples);
    if (capacity <= 0) {
        return true;
    }
    const qsizetype offset = out.size();
    out.resize(offset + qsizetype(capacity) * OutputChannels);
    uint8_t* dst = reinterpret_cast<uint8_t*>(out.data() + offset);

    const int converted = swr_convert(swr, &dst, capacity,
                                      frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr,
                                      inSamples);
    if (converted < 0) {
        out.resize(offset);
        return false;
    }
    out.resize(offset + qsizetype(converted) * OutputChannels);
    return true;
}
#endif

} // namespace

namespace AudioDecoder {

DecodeResult decode(const QString& filePath, int sampleRate)
{
    DecodeResult result;

    if (!QFileInfo::exists(filePath)) {
        result.error = QString("File not found: %1").arg(filePath);
        return result;
    }

#if HAVE_FFMPEG
    DecodeContext ctx;

    if (avformat_open_input(&ctx.format, filePath.toUtf8().constData(), nullptr, nullptr) < 0) {
        result.error = "Could not open the file";
        return result;
    }
    if (avformat_find_stream_info(ctx.format, nullptr) < 0) {
        result.error = "Could not read stream information";
        return result;
    }

    const int streamIndex = av_find_best_stream(ctx.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        result.error = "The file has no audio stream";
        return result;
    }

    const AVCodecParameters* params = ctx.format->streams[streamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        result.error = "Unsupported audio codec";
        return result;
    }
    ctx.codec = avcodec_alloc_context3(codec);
    if (!ctx.codec
        || avcodec_parameters_to_context(ctx.codec, params) < 0
        || avcodec_open2(ctx.codec, codec, nullptr) < 0) {
        result.error = "Could not open the audio decoder";
        return result;
    }

    // Everything is converted to the engine format so clips from different files mix correctly
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&ctx.swr,
                            &stereo, AV_SAMPLE_FMT_S16, sampleRate,
                            &ctx.codec->ch_layout, ctx.codec->sample_fmt, ctx.codec->sample_rate,
                            0, nullptr) < 0
        || swr_init(ctx.swr) < 0) {
        result.error = "Could not initialize the resampler";
        return result;
    }

    ctx.frame = av_frame_alloc();
    ctx.packet = av_packet_alloc();
    if (!ctx.frame || !ctx.packet) {
        result.error = "Out of memory";
        return result;
    }

    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sampleRate = sampleRate;
    buffer->channels = OutputChannels;

    // Reserve from the container's duration estimate to avoid repeated reallocation
    if (ctx.format->duration > 0) {
        const double seconds = double(ctx.format->duration) / AV_TIME_BASE;
        buffer->samples.reserve(qsizetype(seconds * sampleRate * OutputChannels * 1.02));
    }

    auto drainDecoder = [&]() -> bool {
        while (avcodec_receive_frame(ctx.codec, ctx.frame) >= 0) {
            const bool ok = appendConverted(ctx.swr, ctx.frame, buffer->samples);
            av_frame_unref(ctx.frame);
            if (!ok) {
                return false;
            }
        }
        return true;
    };

    while (av_read_frame(ctx.format, ctx.packet) >= 0) {
        if (ctx.packet->stream_index == streamIndex && avcodec_send_packet(ctx.codec, ctx.packet) >= 0) {
            if (!drainDecoder()) {
                av_packet_unref(ctx.packet);
                result.error = "Audio conversion failed";
                return result;
            }
        }
        av_packet_unref(ctx.packet);
    }

    // Flush frames buffered in the decoder, then samples buffered in the resampler
    avcodec_send_packet(ctx.codec, nullptr);
    drainDecoder();
    appendConverted(ctx.swr, nullptr, buffer->samples);

    if (buffer->samples.isEmpty()) {
        result.error = "The file contains no decodable audio";
        return result;
    }
    buffer->samples.squeeze();

    result.peaks = computePeaks(*buffer);
    result.audio = std::move(buffer);
    result.ok = true;
    return result;
#else
    Q_UNUSED(sampleRate)
    result.error = "This build has no FFmpeg support";
    return result;
#endif
}

QStringList supportedExtensions()
{
    return {"wav", "mp3", "flac", "ogg", "opus", "m4a", "aac", "aif", "aiff"};
}

bool isSupportedFile(const QString& filePath)
{
    return supportedExtensions().contains(QFileInfo(filePath).suffix().toLower());
}

QString fileDialogFilter()
{
    QStringList patterns;
    for (const QString& ext : supportedExtensions()) {
        patterns << "*." + ext;
    }
    return QString("Audio Files (%1);;All Files (*)").arg(patterns.join(' '));
}

} // namespace AudioDecoder

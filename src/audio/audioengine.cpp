#include "audioengine.h"
#include "audioiodevice.h"

#include <QAudioDevice>
#include <QDebug>
#include <QMediaDevices>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>

AudioEngine::AudioEngine(ProjectModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_renderList(std::make_shared<RenderList>())
    , m_previewList(std::make_shared<PreviewList>())
{
    m_format.setSampleRate(SampleRate);
    m_format.setChannelCount(Channels);
    m_format.setSampleFormat(QAudioFormat::Int16);

    m_clock.start();

    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(PositionUpdateIntervalMs);
    connect(m_positionTimer, &QTimer::timeout, this, &AudioEngine::updatePosition);

    // Any change to what should be heard rebuilds the render list
    connect(m_model, &ProjectModel::clipAdded, this, &AudioEngine::rebuildRenderList);
    connect(m_model, &ProjectModel::clipChanged, this, &AudioEngine::rebuildRenderList);
    connect(m_model, &ProjectModel::clipRemoved, this, &AudioEngine::rebuildRenderList);
    connect(m_model, &ProjectModel::trackChanged, this, &AudioEngine::rebuildRenderList);
    connect(m_model, &ProjectModel::tempoChanged, this, &AudioEngine::rebuildRenderList);
    connect(m_model, &ProjectModel::projectReset, this, [this]() {
        // Different project: nothing cached for the old tracks may survive
        stop();
        m_effectChains.clear();
        m_instruments.clear();
        m_previewInstruments.clear();
        rebuildRenderList();
    });
    rebuildRenderList();
}

AudioEngine::~AudioEngine()
{
    m_playing = false;
    m_positionTimer->stop();
    if (m_sink) {
        m_sink->stop();
    }
}

std::shared_ptr<EffectChain> AudioEngine::effectChainFor(int track, const QStringList& effects)
{
    CachedChain& cached = m_effectChains[track];
    if (!cached.chain || cached.names != effects) {
        cached.names = effects;
        cached.chain = Effects::createChain(effects, SampleRate);
    }
    return cached.chain;
}

std::shared_ptr<Instrument> AudioEngine::instrumentFor(QHash<int, CachedInstrument>& cache, int track)
{
    const TrackData& data = m_model->track(track);
    CachedInstrument& cached = cache[track];
    if (!cached.instrument || cached.revision != data.instrumentRevision) {
        cached.revision = data.instrumentRevision;
        cached.instrument = std::shared_ptr<Instrument>(InstrumentRegistry::create(data.instrument, SampleRate));
    }
    return cached.instrument;
}

void AudioEngine::rebuildRenderList()
{
    // Group clips and notes by track
    QHash<int, std::vector<RenderClip>> clipsByTrack;
    QHash<int, std::vector<RenderNote>> notesByTrack;
    const double spb = m_model->secondsPerBeat();
    for (int id : m_model->clipIds()) {
        const ClipData* clip = m_model->clip(id);
        if (!clip) {
            continue;
        }
        if (clip->audio) {
            clipsByTrack[clip->track].push_back({clip->audio, qint64(clip->start * SampleRate + 0.5)});
        } else if (clip->isNoteClip()) {
            auto& notes = notesByTrack[clip->track];
            for (const Note& n : clip->notes) {
                if (n.start >= clip->lengthBeats) {
                    continue; // Outside the clip
                }
                const double end = qMin(n.end(), clip->lengthBeats);
                notes.push_back({qint64((clip->start + n.start * spb) * SampleRate + 0.5),
                                 qint64((clip->start + end * spb) * SampleRate + 0.5),
                                 n.pitch, n.velocity / 127.0f});
            }
        }
    }

    auto list = std::make_shared<RenderList>();
    for (int t = 0; t < m_model->trackCount(); ++t) {
        const TrackData& track = m_model->track(t);
        std::shared_ptr<EffectChain> chain = effectChainFor(t, track.effects);
        auto clips = clipsByTrack.take(t);
        auto notes = notesByTrack.take(t);
        // Silent tracks are skipped; an empty track with effects stays so tails can ring out
        if (!m_model->isTrackAudible(t) || (clips.empty() && notes.empty() && chain->isEmpty())) {
            continue;
        }
        std::shared_ptr<Instrument> instrument;
        if (!notes.empty()) {
            instrument = instrumentFor(m_instruments, t);
        }
        // Linear balance pan: the far side fades out, the near side stays at unity
        const float left = track.volume * (track.pan > 0.0f ? 1.0f - track.pan : 1.0f);
        const float right = track.volume * (track.pan < 0.0f ? 1.0f + track.pan : 1.0f);
        list->push_back({std::move(clips), instrument, std::move(notes), chain, left, right});
    }

    QMutexLocker locker(&m_renderMutex);
    m_renderList = std::move(list);
}

qint64 AudioEngine::readMixedData(char* data, qint64 maxlen)
{
    const qint64 bytesPerFrame = Channels * qint64(sizeof(qint16));
    const qint64 frames = maxlen / bytesPerFrame;
    const qint64 bytes = frames * bytesPerFrame;
    if (frames <= 0) {
        return 0;
    }

    const bool playing = m_playing.load();
    const qint64 previewStart = m_previewFrame.fetch_add(frames);

    std::shared_ptr<const RenderList> list;
    std::shared_ptr<const PreviewList> previews;
    {
        QMutexLocker locker(&m_renderMutex);
        list = m_renderList;
        previews = m_previewList;
    }
    if (!playing && previews->empty()) {
        std::memset(data, 0, size_t(bytes));
        return bytes;
    }

    const qint64 blockStart = m_framePosition.load();
    const qint64 blockEnd = blockStart + frames;

    const size_t samples = size_t(frames * Channels);
    m_mixBuffer.assign(samples, 0.0f);
    m_busBuffer.resize(samples);
    float* mix = m_mixBuffer.data();
    float* bus = m_busBuffer.data();

    for (const RenderTrack& track : *list) {
        if (!playing) {
            break;
        }
        // 1. This track's clips into its own bus
        std::fill(bus, bus + samples, 0.0f);
        for (const RenderClip& clip : track.clips) {
            const qint64 from = std::max(blockStart, clip.startFrame);
            const qint64 to = std::min(blockEnd, clip.startFrame + clip.audio->frames());
            if (from >= to) {
                continue;
            }
            const qint16* src = clip.audio->samples.constData() + (from - clip.startFrame) * Channels;
            float* dst = bus + (from - blockStart) * Channels;
            for (qint64 i = 0; i < (to - from) * Channels; ++i) {
                dst[i] += src[i];
            }
        }

        if (track.instrument) {
            track.instrument->render(bus, blockStart, frames, track.notes);
        }

        // 2. The track's own effects, on the track's audio only
        track.effects->process(bus, frames);

        // 3. Track volume + pan, summed into the output
        for (qint64 f = 0; f < frames; ++f) {
            mix[f * 2] += bus[f * 2] * track.gainLeft;
            mix[f * 2 + 1] += bus[f * 2 + 1] * track.gainRight;
        }
    }

    // Auditioned notes, on their own clock so they sound whether or not the song plays
    for (const PreviewTrack& preview : *previews) {
        std::fill(bus, bus + samples, 0.0f);
        if (preview.instrument) {
            preview.instrument->render(bus, previewStart, frames, preview.notes);
        }
        for (qint64 f = 0; f < frames; ++f) {
            mix[f * 2] += bus[f * 2] * preview.gainLeft;
            mix[f * 2 + 1] += bus[f * 2 + 1] * preview.gainRight;
        }
    }

    qint16* out = reinterpret_cast<qint16*>(data);
    for (qint64 i = 0; i < frames * Channels; ++i) {
        out[i] = qint16(std::clamp(mix[i], -32768.0f, 32767.0f));
    }

    if (playing) {
        m_framePosition.store(blockEnd);
    }
    return bytes;
}

void AudioEngine::setupAudioOutput()
{
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    delete m_device;

    // A fresh sink follows the current default output (e.g. headphones plugged in later)
    m_sink = new QAudioSink(m_format, this);
    m_sink->setBufferSize(8192);
    applyMasterVolume();
    m_device = new AudioIODevice(this, this);
}

void AudioEngine::anchorClock()
{
    m_anchorPosition = m_position;
    m_anchorClockMs = m_clock.elapsed();
}

void AudioEngine::play()
{
    if (m_playing.load()) {
        return;
    }
    if (!m_model->hasClips()) {
        emit audioError(AudioError::FileNotFound, "No audio clips loaded");
        return;
    }
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        emit audioError(AudioError::DeviceError,
                        "No audio output device found. Connect headphones or speakers, then press play again.");
        return;
    }

    // Playing from the end restarts from the top
    if (m_model->length() > 0 && m_position >= m_model->length()) {
        m_position = 0.0;
    }
    m_framePosition = qint64(m_position * SampleRate + 0.5);
    anchorClock();

    if (m_paused && m_sink && m_sink->state() == QAudio::SuspendedState) {
        m_playing = true;
        m_sink->resume();
    } else {
        setupAudioOutput();
        m_playing = true; // before start(), so the first pull already has audio
        m_sink->start(m_device);
        if (m_sink->error() != QAudio::NoError && m_sink->state() == QAudio::StoppedState) {
            m_playing = false;
            emit audioError(AudioError::DeviceError, "Failed to start audio output");
            return;
        }
    }

    m_paused = false;
    m_positionTimer->start();
    emit playbackStateChanged(true);
}

void AudioEngine::pause()
{
    if (!m_playing.load()) {
        return;
    }
    m_playing = false;
    m_positionTimer->stop();
    updatePosition(); // settle on the exact position
    if (m_sink) {
        m_sink->suspend();
    }
    m_paused = true;
    emit playbackStateChanged(false);
}

void AudioEngine::stop()
{
    const bool wasPlaying = m_playing.exchange(false);
    m_positionTimer->stop();
    if (m_sink) {
        m_sink->stop();
    }
    m_paused = false;
    m_position = 0.0;
    m_framePosition = 0;

    if (wasPlaying) {
        emit playbackStateChanged(false);
    }
    emit positionChanged(0.0);
}

void AudioEngine::updatePosition()
{
    if (!m_playing.load()) {
        return;
    }
    // Smooth playhead from a monotonic clock anchored at play/seek time
    m_position = m_anchorPosition + (m_clock.elapsed() - m_anchorClockMs) / 1000.0;

    if (m_model->length() > 0 && m_position >= m_model->length()) {
        stop();
        return;
    }
    emit positionChanged(m_position);
}

void AudioEngine::setTimelinePosition(double seconds)
{
    // Seeking works while playing too: the mixer and the playhead clock move together.
    // (Programmatic playhead updates never emit back into the engine, so no feedback loop.)
    m_position = qMax(0.0, seconds);
    m_framePosition = qint64(m_position * SampleRate + 0.5);
    anchorClock();
}

void AudioEngine::onTransportPlay() { play(); }
void AudioEngine::onTransportStop() { pause(); }
void AudioEngine::onTransportPause() { pause(); }
void AudioEngine::onTransportStopAndReturn() { stop(); }
void AudioEngine::onPositionChanged(double seconds) { setTimelinePosition(seconds); }

void AudioEngine::ensureOutputRunning()
{
    if (m_sink && m_sink->state() == QAudio::SuspendedState) {
        m_sink->resume(); // Paused: the mixer only outputs previews while not playing
        return;
    }
    if (!m_sink || m_sink->state() == QAudio::StoppedState) {
        setupAudioOutput();
        m_sink->start(m_device);
    }
}

void AudioEngine::previewNote(int track, int pitch, int velocity, double seconds)
{
    if (!m_model->isValidTrack(track)) {
        return;
    }
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        return; // Nowhere to play it; auditioning stays silent rather than nagging
    }
    ensureOutputRunning();

    const TrackData& data = m_model->track(track);
    std::shared_ptr<Instrument> instrument = instrumentFor(m_previewInstruments, track);
    const qint64 now = m_previewFrame.load();

    // Rebuild the preview list: drop notes that have fully rung out, add the new one
    PreviewTrack mine{track, instrument, {},
                      data.volume * (data.pan > 0.0f ? 1.0f - data.pan : 1.0f),
                      data.volume * (data.pan < 0.0f ? 1.0f + data.pan : 1.0f)};
    auto list = std::make_shared<PreviewList>();
    {
        QMutexLocker locker(&m_renderMutex);
        for (const PreviewTrack& p : *m_previewList) {
            PreviewTrack kept{p.track, p.instrument, {}, p.gainLeft, p.gainRight};
            for (const RenderNote& n : p.notes) {
                if (p.instrument && n.endFrame + p.instrument->tailFrames() > now) {
                    kept.notes.push_back(n);
                }
            }
            if (p.instrument == instrument) {
                mine.notes.insert(mine.notes.end(), kept.notes.begin(), kept.notes.end());
            } else if (!kept.notes.empty()) {
                list->push_back(std::move(kept));
            }
        }
    }
    mine.notes.push_back({now, now + qint64(seconds * SampleRate), pitch, qBound(1, velocity, 127) / 127.0f});
    list->push_back(std::move(mine));

    QMutexLocker locker(&m_renderMutex);
    m_previewList = std::move(list);
}

void AudioEngine::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    applyMasterVolume();
}

void AudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    applyMasterVolume();
}

void AudioEngine::applyMasterVolume()
{
    if (m_sink) {
        m_sink->setVolume(m_muted ? 0.0f : m_volume);
    }
}

#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "../core/projectmodel.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct DecodeResult {
    bool ok = false;
    QString error;
    std::shared_ptr<const AudioBuffer> audio;
    QVector<qreal> peaks; // normalized 0..1, PeaksPerSecond values per second
};

// Stateless FFmpeg decoder. Safe to call from any thread (every call owns its
// FFmpeg contexts), so imports run off the UI thread via QtConcurrent.
namespace AudioDecoder {

constexpr int PeaksPerSecond = 200;

// Decodes a whole file, resampled to the given rate as interleaved 16-bit stereo.
DecodeResult decode(const QString& filePath, int sampleRate);

// Extensions offered in the file picker and accepted on drag & drop (lowercase, no dot)
QStringList supportedExtensions();
bool isSupportedFile(const QString& filePath);
QString fileDialogFilter();

} // namespace AudioDecoder

#endif // AUDIODECODER_H

#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H

#include <QVector>
#include <QtGlobal>

// Decoded audio, interleaved 16-bit PCM at the engine's sample rate.
// Immutable once decoded, so it can be shared with the audio thread safely.
struct AudioBuffer {
    int sampleRate = 0;
    int channels = 0;
    QVector<qint16> samples;

    qint64 frames() const { return channels > 0 ? samples.size() / channels : 0; }
    double duration() const { return sampleRate > 0 ? double(frames()) / sampleRate : 0.0; }
};

#endif // AUDIOBUFFER_H

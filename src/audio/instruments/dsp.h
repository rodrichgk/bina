#ifndef INSTRUMENT_DSP_H
#define INSTRUMENT_DSP_H

// Small DSP building blocks shared by instruments. Header-only, Qt Core only.

#include <QtGlobal>
#include <QtMath>
#include <cmath>
#include <cstdint>

namespace Dsp {

constexpr int Channels = 2;

inline double noteFrequency(int pitch)
{
    return 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
}

// Band-limited step correction for saw/square oscillators (t = phase 0..1, dt = phase increment)
inline double polyBlep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// ADSR as a pure function of time since note start, so it needs no state.
struct Adsr {
    qint64 attack = 1;  // frames
    qint64 decay = 1;   // frames
    float sustain = 1;  // level 0..1
    qint64 release = 1; // frames

    static Adsr fromSeconds(double a, double d, double s, double r, int sampleRate)
    {
        Adsr e;
        e.attack = qMax<qint64>(1, qint64(a * sampleRate));
        e.decay = qMax<qint64>(1, qint64(d * sampleRate));
        e.sustain = float(qBound(0.0, s, 1.0));
        e.release = qMax<qint64>(1, qint64(r * sampleRate));
        return e;
    }

    float held(qint64 t) const
    {
        if (t < attack) {
            return float(t) / attack;
        }
        t -= attack;
        if (t < decay) {
            return 1.0f - (1.0f - sustain) * float(t) / decay;
        }
        return sustain;
    }

    // t = frames since note on, noteFrames = note length in frames
    float level(qint64 t, qint64 noteFrames) const
    {
        if (t < noteFrames) {
            return held(t);
        }
        const float r = 1.0f - float(t - noteFrames) / release;
        return r > 0.0f ? held(noteFrames) * r : 0.0f;
    }
};

// Two cascaded one-pole low-passes per channel (12 dB/oct). Keeps state.
class StereoLowpass
{
public:
    void setCutoff(double hz, int sampleRate)
    {
        hz = qBound(20.0, hz, sampleRate * 0.45);
        m_a = float(std::exp(-2.0 * M_PI * hz / sampleRate));
    }
    void reset()
    {
        m_s1[0] = m_s1[1] = m_s2[0] = m_s2[1] = 0.0f;
    }
    void process(float* interleaved, qint64 frames)
    {
        for (qint64 f = 0; f < frames; ++f) {
            for (int c = 0; c < Channels; ++c) {
                float& x = interleaved[f * Channels + c];
                m_s1[c] = (1.0f - m_a) * x + m_a * m_s1[c];
                m_s2[c] = (1.0f - m_a) * m_s1[c] + m_a * m_s2[c];
                x = m_s2[c];
            }
        }
    }

private:
    float m_a = 0.0f;
    float m_s1[Channels] = {0, 0};
    float m_s2[Channels] = {0, 0};
};

// Deterministic white noise in [-1, 1]; seed it per note for repeatable hits
struct Noise {
    uint32_t state = 0x12345678u;
    explicit Noise(uint32_t seed = 0x12345678u) : state(seed ? seed : 1u) {}
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(int32_t(state)) / 2147483648.0f;
    }
};

} // namespace Dsp

#endif // INSTRUMENT_DSP_H

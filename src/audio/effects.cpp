#include "effects.h"

#include <QtMath>
#include <cmath>

namespace {

constexpr int Channels = 2;

// One-pole low-pass per channel, cascaded twice for a 12 dB/oct slope
class LowPassFilter : public AudioEffect
{
public:
    LowPassFilter(double cutoffHz, int sampleRate)
        : m_a(float(std::exp(-2.0 * M_PI * cutoffHz / sampleRate))) {}

    void process(float* buffer, qint64 frames) override
    {
        for (qint64 f = 0; f < frames; ++f) {
            for (int c = 0; c < Channels; ++c) {
                float& x = buffer[f * Channels + c];
                m_s1[c] = (1.0f - m_a) * x + m_a * m_s1[c];
                m_s2[c] = (1.0f - m_a) * m_s1[c] + m_a * m_s2[c];
                x = m_s2[c];
            }
        }
    }

private:
    float m_a;
    float m_s1[Channels] = {0, 0};
    float m_s2[Channels] = {0, 0};
};

// Input minus its own one-pole low-pass, cascaded twice
class HighPassFilter : public AudioEffect
{
public:
    HighPassFilter(double cutoffHz, int sampleRate)
        : m_a(float(std::exp(-2.0 * M_PI * cutoffHz / sampleRate))) {}

    void process(float* buffer, qint64 frames) override
    {
        for (qint64 f = 0; f < frames; ++f) {
            for (int c = 0; c < Channels; ++c) {
                float& x = buffer[f * Channels + c];
                m_l1[c] = (1.0f - m_a) * x + m_a * m_l1[c];
                const float h1 = x - m_l1[c];
                m_l2[c] = (1.0f - m_a) * h1 + m_a * m_l2[c];
                x = h1 - m_l2[c];
            }
        }
    }

private:
    float m_a;
    float m_l1[Channels] = {0, 0};
    float m_l2[Channels] = {0, 0};
};

// Feedback echo
class Delay : public AudioEffect
{
public:
    Delay(double seconds, float feedback, float mix, int sampleRate)
        : m_line(size_t(seconds * sampleRate) * Channels, 0.0f)
        , m_feedback(feedback)
        , m_mix(mix) {}

    void process(float* buffer, qint64 frames) override
    {
        for (qint64 i = 0; i < frames * Channels; ++i) {
            const float delayed = m_line[m_pos];
            m_line[m_pos] = buffer[i] + delayed * m_feedback;
            buffer[i] += delayed * m_mix;
            if (++m_pos == m_line.size()) {
                m_pos = 0;
            }
        }
    }

private:
    std::vector<float> m_line;
    size_t m_pos = 0;
    float m_feedback;
    float m_mix;
};

// Soft clipping
class Distortion : public AudioEffect
{
public:
    Distortion(float drive, float output) : m_drive(drive), m_output(output) {}

    void process(float* buffer, qint64 frames) override
    {
        for (qint64 i = 0; i < frames * Channels; ++i) {
            buffer[i] = std::tanh(buffer[i] / 32768.0f * m_drive) * 32768.0f * m_output;
        }
    }

private:
    float m_drive;
    float m_output;
};

// Amplitude modulation
class Tremolo : public AudioEffect
{
public:
    Tremolo(double rateHz, float depth, int sampleRate)
        : m_step(2.0 * M_PI * rateHz / sampleRate), m_depth(depth) {}

    void process(float* buffer, qint64 frames) override
    {
        for (qint64 f = 0; f < frames; ++f) {
            const float gain = 1.0f - m_depth * 0.5f * float(1.0 + std::sin(m_phase));
            buffer[f * Channels] *= gain;
            buffer[f * Channels + 1] *= gain;
            m_phase += m_step;
            if (m_phase > 2.0 * M_PI) {
                m_phase -= 2.0 * M_PI;
            }
        }
    }

private:
    double m_phase = 0.0;
    double m_step;
    float m_depth;
};

std::unique_ptr<AudioEffect> create(const QString& name, int sampleRate)
{
    if (name == "Low Pass Filter")  return std::make_unique<LowPassFilter>(900.0, sampleRate);
    if (name == "High Pass Filter") return std::make_unique<HighPassFilter>(400.0, sampleRate);
    if (name == "Delay")            return std::make_unique<Delay>(0.35, 0.35f, 0.35f, sampleRate);
    if (name == "Distortion")       return std::make_unique<Distortion>(4.0f, 0.6f);
    if (name == "Tremolo")          return std::make_unique<Tremolo>(5.0, 0.7f, sampleRate);
    return nullptr;
}

} // namespace

namespace Effects {

QStringList available()
{
    return {"Low Pass Filter", "High Pass Filter", "Delay", "Distortion", "Tremolo"};
}

std::shared_ptr<EffectChain> createChain(const QStringList& names, int sampleRate)
{
    std::vector<std::unique_ptr<AudioEffect>> effects;
    for (const QString& name : names) {
        if (auto effect = create(name, sampleRate)) {
            effects.push_back(std::move(effect));
        }
    }
    return std::make_shared<EffectChain>(std::move(effects));
}

} // namespace Effects

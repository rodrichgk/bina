// Synth: one band-limited oscillator per note, ADSR, low-pass on the output.
// Reference implementation of the instrument API (see instrumentapi.h).

#include "dsp.h"
#include "instrumentapi.h"

#include <algorithm>

namespace {

enum Waveform { Sine, Triangle, Saw, Square };

class Synth : public Instrument
{
public:
    explicit Synth(const InstrumentContext& ctx)
        : m_sampleRate(ctx.sampleRate)
        , m_waveform(ctx.choice("waveform"))
        , m_env(Dsp::Adsr::fromSeconds(ctx.param("attack"), ctx.param("decay"),
                                       ctx.param("sustain"), ctx.param("release"), ctx.sampleRate))
    {
        m_filter.setCutoff(ctx.param("cutoff"), ctx.sampleRate);
    }

    qint64 tailFrames() const override { return m_env.release; }

    void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) override
    {
        m_voices.assign(size_t(frames * Dsp::Channels), 0.0f);
        float* out = m_voices.data();
        const qint64 blockEnd = blockStart + frames;

        for (const RenderNote& note : notes) {
            const qint64 from = std::max(blockStart, note.startFrame);
            const qint64 to = std::min(blockEnd, note.endFrame + m_env.release);
            if (from >= to) {
                continue;
            }
            const qint64 noteFrames = note.endFrame - note.startFrame;
            const double increment = Dsp::noteFrequency(note.pitch) / m_sampleRate;
            const float gain = 5200.0f * note.velocity;
            for (qint64 f = from; f < to; ++f) {
                const qint64 t = f - note.startFrame;
                // Phase from time since note on: no oscillator state, seeking is free
                const double phase = std::fmod(t * increment, 1.0);
                const float v = oscillator(phase, increment) * m_env.level(t, noteFrames) * gain;
                out[(f - blockStart) * 2] += v;
                out[(f - blockStart) * 2 + 1] += v;
            }
        }

        // The filter runs every block so its state decays naturally
        m_filter.process(out, frames);
        for (qint64 i = 0; i < frames * Dsp::Channels; ++i) {
            bus[i] += out[i];
        }
    }

private:
    float oscillator(double phase, double increment) const
    {
        switch (m_waveform) {
        case Sine:
            return float(std::sin(2.0 * M_PI * phase));
        case Triangle:
            return float(4.0 * std::abs(phase - 0.5) - 1.0);
        case Saw:
            return float(2.0 * phase - 1.0 - Dsp::polyBlep(phase, increment));
        case Square:
        default: {
            double v = phase < 0.5 ? 1.0 : -1.0;
            v += Dsp::polyBlep(phase, increment);
            v -= Dsp::polyBlep(std::fmod(phase + 0.5, 1.0), increment);
            return float(v);
        }
        }
    }

    int m_sampleRate;
    int m_waveform;
    Dsp::Adsr m_env;
    Dsp::StereoLowpass m_filter;
    std::vector<float> m_voices;
};

InstrumentDefinition makeDefinition()
{
    InstrumentDefinition d;
    d.id = "synth";
    d.name = "Synth";
    d.description = "One oscillator with an envelope and a low-pass filter.";
    d.sortOrder = 0;
    d.params = {
        InstrumentParam::choice("waveform", "Waveform", {"Sine", "Triangle", "Saw", "Square"}, Saw),
        InstrumentParam::continuous("attack", "Attack", 0.0, 2.0, 0.005, "ms", 1000, 0, true),
        InstrumentParam::continuous("decay", "Decay", 0.0, 2.0, 0.2, "ms", 1000, 0, true),
        InstrumentParam::continuous("sustain", "Sustain", 0.0, 1.0, 0.7, "%", 100),
        InstrumentParam::continuous("release", "Release", 0.0, 4.0, 0.25, "ms", 1000, 0, true),
        InstrumentParam::continuous("cutoff", "Filter", 80.0, 18000.0, 8000.0, "Hz", 1, 0, true),
    };
    d.create = [](const InstrumentContext& ctx) { return std::make_unique<Synth>(ctx); };
    return d;
}

} // namespace

REGISTER_INSTRUMENT(makeDefinition())

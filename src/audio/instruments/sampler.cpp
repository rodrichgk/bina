// Sampler: one loaded audio file, pitched across the keyboard, with ADSR and low-pass.

#include "dsp.h"
#include "instrumentapi.h"

#include <algorithm>

namespace {

constexpr int LowestRoot = 24; // C1

QStringList rootNoteNames()
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    QStringList list;
    for (int pitch = LowestRoot; pitch <= 96; ++pitch) {
        list << QString("%1%2").arg(names[pitch % 12]).arg(pitch / 12 - 1);
    }
    return list;
}

class Sampler : public Instrument
{
public:
    explicit Sampler(const InstrumentContext& ctx)
        : m_sampleRate(ctx.sampleRate)
        , m_sample(ctx.settings.sample)
        , m_rootNote(LowestRoot + ctx.choice("root"))
        , m_env(Dsp::Adsr::fromSeconds(ctx.param("attack"), ctx.param("decay"),
                                       ctx.param("sustain"), ctx.param("release"), ctx.sampleRate))
    {
        m_filter.setCutoff(ctx.param("cutoff"), ctx.sampleRate);
    }

    qint64 tailFrames() const override { return m_env.release; }

    void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) override
    {
        if (!m_sample || m_sample->frames() < 2) {
            return; // Nothing loaded: silent
        }
        m_voices.assign(size_t(frames * Dsp::Channels), 0.0f);
        float* out = m_voices.data();
        const qint64 blockEnd = blockStart + frames;
        const qint16* s = m_sample->samples.constData();
        const qint64 sampleFrames = m_sample->frames();
        const int sc = m_sample->channels;

        for (const RenderNote& note : notes) {
            const qint64 from = std::max(blockStart, note.startFrame);
            const qint64 to = std::min(blockEnd, note.endFrame + m_env.release);
            if (from >= to) {
                continue;
            }
            const qint64 noteFrames = note.endFrame - note.startFrame;
            // Resampled so the root note plays the sample unchanged
            const double ratio = std::pow(2.0, (note.pitch - m_rootNote) / 12.0)
                                 * double(m_sample->sampleRate) / m_sampleRate;
            for (qint64 f = from; f < to; ++f) {
                const qint64 t = f - note.startFrame;
                const double pos = t * ratio;
                const qint64 i = qint64(pos);
                if (i + 1 >= sampleFrames) {
                    break;
                }
                const float frac = float(pos - i);
                const float env = m_env.level(t, noteFrames) * note.velocity * 0.8f;
                for (int c = 0; c < Dsp::Channels; ++c) {
                    const int ch = qMin(c, sc - 1);
                    const float a = s[i * sc + ch];
                    const float b = s[(i + 1) * sc + ch];
                    out[(f - blockStart) * 2 + c] += (a + (b - a) * frac) * env;
                }
            }
        }

        m_filter.process(out, frames);
        for (qint64 i = 0; i < frames * Dsp::Channels; ++i) {
            bus[i] += out[i];
        }
    }

private:
    int m_sampleRate;
    std::shared_ptr<const AudioBuffer> m_sample;
    int m_rootNote;
    Dsp::Adsr m_env;
    Dsp::StereoLowpass m_filter;
    std::vector<float> m_voices;
};

InstrumentDefinition makeDefinition()
{
    InstrumentDefinition d;
    d.id = "sampler";
    d.name = "Sampler";
    d.description = "Plays a loaded audio file, pitched across the keyboard.";
    d.sortOrder = 1;
    d.usesSample = true;
    d.params = {
        InstrumentParam::choice("root", "Root Note", rootNoteNames(), 60 - LowestRoot),
        InstrumentParam::continuous("attack", "Attack", 0.0, 2.0, 0.002, "ms", 1000, 0, true),
        InstrumentParam::continuous("decay", "Decay", 0.0, 2.0, 0.2, "ms", 1000, 0, true),
        InstrumentParam::continuous("sustain", "Sustain", 0.0, 1.0, 1.0, "%", 100),
        InstrumentParam::continuous("release", "Release", 0.0, 4.0, 0.3, "ms", 1000, 0, true),
        InstrumentParam::continuous("cutoff", "Filter", 80.0, 18000.0, 18000.0, "Hz", 1, 0, true),
    };
    d.create = [](const InstrumentContext& ctx) { return std::make_unique<Sampler>(ctx); };
    return d;
}

} // namespace

REGISTER_INSTRUMENT(makeDefinition())

// FM Synth: two-operator phase modulation (modulator -> carrier) with a decaying
// modulation index, plus an optional "tine" pair (carrier modulated at 14x) for the
// metallic attack of an electric piano. DX-style, tuned by default as an electric piano.

#include "dsp.h"
#include "instrumentapi.h"

#include <algorithm>

namespace {

const double Ratios[] = {0.5, 1.0, 1.5, 2.0, 3.0, 3.5, 4.0, 5.0, 7.0, 9.0, 14.0};
constexpr int RatioCount = int(sizeof(Ratios) / sizeof(Ratios[0]));
constexpr int DefaultRatio = 1; // 1:1, the classic electric piano body

constexpr double TineRatio = 14.0;
constexpr double TineDetuneCents = 3.0; // tine pair only, so the body stays mono-solid
constexpr double IndexFloor = 0.15;     // brightness never decays all the way to a pure sine
constexpr double MaxFeedback = 1.5;     // radians; beyond this the averaged loop gets noisy
constexpr float Gain = 6400.0f; // worst-case voice peak, so a 5-note chord stays under full scale

QStringList ratioNames()
{
    QStringList list;
    for (double r : Ratios) {
        list << QString("%1").arg(r);
    }
    return list;
}

// Largest index that keeps the main sidebands (Carson's rule) below ~0.45 * sampleRate.
// Returns 0 when the modulator itself would alias.
double indexLimit(double carrierHz, double modulatorHz, int sampleRate)
{
    const double ceiling = 0.45 * sampleRate;
    if (modulatorHz >= ceiling) {
        return 0.0;
    }
    return std::max(0.0, (ceiling - carrierHz) / modulatorHz - 1.0);
}

// Amplitude envelope with exponential decay/release (more piano-like than linear).
// A pure function of time since note on; release fades to exactly zero at its end.
struct Envelope {
    qint64 attack = 1;
    double decayRate = 0.0; // per frame
    float sustain = 0.0f;
    qint64 release = 1;

    float held(qint64 t) const
    {
        if (t < attack) {
            return float(t) / attack;
        }
        return sustain + (1.0f - sustain) * float(std::exp(-double(t - attack) * decayRate));
    }

    float level(qint64 t, qint64 noteFrames) const
    {
        if (t < noteFrames) {
            return held(t);
        }
        const double r = double(t - noteFrames) / release;
        if (r >= 1.0) {
            return 0.0f;
        }
        return held(noteFrames) * float(std::exp(-5.0 * r) * (1.0 - r));
    }
};

class FmSynth : public Instrument
{
public:
    explicit FmSynth(const InstrumentContext& ctx)
        : m_sampleRate(ctx.sampleRate)
        , m_ratio(Ratios[qBound(0, ctx.choice("ratio"), RatioCount - 1)])
        , m_brightness(qBound(0.0, ctx.param("brightness"), 10.0))
        , m_brightDecay(qMax(0.001, ctx.param("brightdecay")))
        , m_feedback(qBound(0.0, ctx.param("feedback"), 1.0))
        , m_velocitySense(qBound(0.0, ctx.param("velsense"), 1.0))
        , m_tine(qBound(0.0, ctx.param("tine"), 1.0))
        , m_attack(qMax<qint64>(1, qint64(qMax(0.0, ctx.param("attack")) * ctx.sampleRate)))
        , m_decay(qMax(0.001, ctx.param("decay")))
        , m_sustain(float(qBound(0.0, ctx.param("sustain"), 1.0)))
        , m_release(qMax<qint64>(1, qint64(qMax(0.0, ctx.param("release")) * ctx.sampleRate)))
    {
        // ~15 Hz DC blocker: ratio 0.5 puts a sideband right at 0 Hz
        m_dcCoef = float(std::exp(-2.0 * M_PI * 15.0 / ctx.sampleRate));
    }

    qint64 tailFrames() const override { return m_release; }

    void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) override
    {
        m_mix.assign(size_t(frames * Dsp::Channels), 0.0f);
        float* out = m_mix.data();
        const qint64 blockEnd = blockStart + frames;

        // Feedback history is the only per-voice state; a seek starts it fresh
        if (blockStart != m_expectedStart) {
            m_voices.clear();
        }
        m_nextVoices.clear();

        for (const RenderNote& note : notes) {
            const qint64 from = std::max(blockStart, note.startFrame);
            const qint64 to = std::min(blockEnd, note.endFrame + m_release);
            if (from >= to) {
                continue;
            }
            Voice voice{note.startFrame, note.pitch, 0.0f, 0.0f};
            for (const Voice& v : m_voices) {
                if (v.start == note.startFrame && v.pitch == note.pitch) {
                    voice = v;
                    break;
                }
            }
            renderNote(note, from, to, blockStart, voice, out);
            m_nextVoices.push_back(voice);
        }
        std::swap(m_voices, m_nextVoices);
        m_expectedStart = blockEnd;

        for (qint64 f = 0; f < frames; ++f) {
            for (int c = 0; c < Dsp::Channels; ++c) {
                const float x = out[f * Dsp::Channels + c];
                const float y = x - m_dcIn[c] + m_dcCoef * m_dcOut[c];
                m_dcIn[c] = x;
                m_dcOut[c] = y;
                bus[f * Dsp::Channels + c] += y;
            }
        }
    }

private:
    struct Voice {
        qint64 start;
        int pitch;
        float y1; // last two modulator outputs, averaged for a stable feedback loop
        float y2;
    };

    void renderNote(const RenderNote& note, qint64 from, qint64 to, qint64 blockStart, Voice& voice, float* out) const
    {
        const double sr = m_sampleRate;
        const double f0 = Dsp::noteFrequency(note.pitch);
        const double modHz = f0 * m_ratio;
        const float velocity = qBound(0.0f, note.velocity, 1.0f);

        // Higher notes decay faster, as on a real piano (halves every two octaves)
        const double keyScale = qBound(0.35, std::pow(2.0, -(note.pitch - 60) / 24.0), 2.0);
        Envelope env;
        env.attack = m_attack;
        env.decayRate = std::log(1000.0) / qMax(1.0, m_decay * keyScale * sr);
        env.sustain = m_sustain;
        env.release = m_release;

        // Feedback thickens the modulator's spectrum, so it also shrinks the safe index
        const double fb = m_feedback * qBound(0.0, (0.45 * sr / modHz - 2.0) / 6.0, 1.0) * MaxFeedback;
        const double effectiveModHz = modHz * (1.0 + fb);
        // Velocity drives brightness; the top of the keyboard gets a gentler index
        const double velIndex = (1.0 - m_velocitySense) + m_velocitySense * velocity;
        const double keyIndex = note.pitch > 72 ? std::pow(2.0, -(note.pitch - 72) / 24.0) : 1.0;
        const double peakIndex = std::min(m_brightness * velIndex * keyIndex,
                                          indexLimit(f0, effectiveModHz, m_sampleRate));
        const double indexRate = 3.0 / qMax(1.0, m_brightDecay * keyScale * sr);

        const bool tineOn = m_tine > 0.0;
        const double tinePeakIndex = std::min(1.8 * (0.3 + 0.7 * velocity) * keyIndex,
                                              indexLimit(f0, f0 * TineRatio, m_sampleRate));
        const double tineIndexRate = 1.0 / (0.06 * keyScale * sr);
        const double tineAmpRate = 1.0 / (0.35 * keyScale * sr);
        const float tineLevel = float(0.8 * m_tine);
        const float norm = Gain * velocity / (1.0f + tineLevel);

        const double carrierInc = f0 / sr;
        const double modInc = modHz / sr;
        const double tineModInc = f0 * TineRatio / sr;
        const double detune = std::pow(2.0, TineDetuneCents / 1200.0);
        const double tineIncL = carrierInc / detune;
        const double tineIncR = carrierInc * detune;
        const qint64 noteFrames = note.endFrame - note.startFrame;
        const double twoPi = 2.0 * M_PI;

        for (qint64 f = from; f < to; ++f) {
            const qint64 t = f - note.startFrame;
            const float amp = env.level(t, noteFrames) * norm;
            const double attackRamp = t < m_attack ? double(t) / m_attack : 1.0;

            // Body: modulator (with optional self-feedback) -> carrier
            const double index = peakIndex * attackRamp
                                 * (IndexFloor + (1.0 - IndexFloor) * std::exp(-double(t) * indexRate));
            const double modPhase = std::fmod(t * modInc, 1.0);
            const double mod = std::sin(twoPi * modPhase + fb * 0.5 * (voice.y1 + voice.y2));
            voice.y2 = voice.y1;
            voice.y1 = float(mod);
            const double body = std::sin(twoPi * std::fmod(t * carrierInc, 1.0) + index * mod);

            float left = float(body);
            float right = left;
            if (tineOn) {
                const double tineAmp = std::exp(-double(t) * tineAmpRate);
                if (tineAmp > 1e-4) {
                    const double tineIndex = tinePeakIndex * std::exp(-double(t) * tineIndexRate);
                    const double tineMod = tineIndex * std::sin(twoPi * std::fmod(t * tineModInc, 1.0));
                    const float level = tineLevel * float(tineAmp);
                    left += level * float(std::sin(twoPi * std::fmod(t * tineIncL, 1.0) + tineMod));
                    right += level * float(std::sin(twoPi * std::fmod(t * tineIncR, 1.0) + tineMod));
                }
            }
            out[(f - blockStart) * 2] += left * amp;
            out[(f - blockStart) * 2 + 1] += right * amp;
        }
    }

    int m_sampleRate;
    double m_ratio;
    double m_brightness;
    double m_brightDecay;
    double m_feedback;
    double m_velocitySense;
    double m_tine;
    qint64 m_attack;
    double m_decay;
    float m_sustain;
    qint64 m_release;

    float m_dcCoef = 0.0f;
    float m_dcIn[Dsp::Channels] = {0, 0};
    float m_dcOut[Dsp::Channels] = {0, 0};

    std::vector<float> m_mix;
    std::vector<Voice> m_voices;
    std::vector<Voice> m_nextVoices;
    qint64 m_expectedStart = -1;
};

InstrumentDefinition makeDefinition()
{
    InstrumentDefinition d;
    d.id = "fm";
    d.name = "FM Synth";
    d.description = "Two-operator FM with a decaying brightness and a tine layer, from electric piano to bells and basses.";
    d.sortOrder = 20;
    d.params = {
        InstrumentParam::choice("ratio", "Ratio", ratioNames(), DefaultRatio),
        InstrumentParam::continuous("brightness", "Brightness", 0.0, 10.0, 2.0, {}, 1.0, 1),
        InstrumentParam::continuous("brightdecay", "Bright Decay", 0.02, 8.0, 0.6, "ms", 1000, 0, true),
        InstrumentParam::continuous("feedback", "Feedback", 0.0, 1.0, 0.0, "%", 100),
        InstrumentParam::continuous("velsense", "Velocity", 0.0, 1.0, 0.7, "%", 100),
        InstrumentParam::continuous("tine", "Tine", 0.0, 1.0, 0.4, "%", 100),
        InstrumentParam::continuous("attack", "Attack", 0.001, 2.0, 0.002, "ms", 1000, 0, true),
        InstrumentParam::continuous("decay", "Decay", 0.05, 10.0, 4.0, "ms", 1000, 0, true),
        InstrumentParam::continuous("sustain", "Sustain", 0.0, 1.0, 0.0, "%", 100),
        InstrumentParam::continuous("release", "Release", 0.005, 4.0, 0.3, "ms", 1000, 0, true),
    };
    d.create = [](const InstrumentContext& ctx) { return std::make_unique<FmSynth>(ctx); };
    return d;
}

} // namespace

REGISTER_INSTRUMENT(makeDefinition())

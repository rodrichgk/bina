// Plucked Strings: extended Karplus-Strong waveguide. Each voice is a delay line
// tuned with an allpass, a one-pole damping filter in the loop and a noise burst
// excitation shaped by pick tone and pick position. A small resonant body and a
// mid/side pickup spread are applied to the mix.

#include "dsp.h"
#include "instrumentapi.h"

#include <algorithm>

namespace {

constexpr int MaxVoices = 48;
constexpr int LowestPitch = 12;     // lower notes play at this pitch; bounds the delay lines
constexpr double LowestHz = 16.0;   // just under noteFrequency(LowestPitch)
constexpr double VoiceGain = 2600.0;
constexpr double SilentDb = -75.0;  // a voice is dropped once it has decayed this far

// Two-pole band-pass (RBJ, 0 dB peak), used for the body resonances
struct Resonator {
    float b0 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void setup(double hz, double q, int sampleRate)
    {
        const double w = 2.0 * M_PI * hz / sampleRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = float(alpha / a0);
        a1 = float(-2.0 * std::cos(w) / a0);
        a2 = float((1.0 - alpha) / a0);
    }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }
    float process(float x)
    {
        const float y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Voice {
    bool active = false;
    bool seen = false;
    qint64 startFrame = 0;
    int pitch = 0;

    std::vector<float> line; // allocated once, `length` of it is used
    int length = 2;
    int pos = 0;
    int sideTap = 1;

    float apCoef = 0, apX1 = 0, apY1 = 0; // fractional delay allpass
    float lpCoef = 0, lpY1 = 0;           // loop damping
    float heldGain = 0;                   // loop gain per pass while the key is held
    float releasedGain = 0;               // ... and after it is released
    qint64 noteFrames = 0;
    qint64 lifeFrames = 0;                // frames after startFrame until silent
    float gain = 0;
    qint64 fadeIn = 0;                    // frames of fade-in for voices started mid-note
    qint64 fadeDone = 0;
};

class Pluck : public Instrument
{
public:
    explicit Pluck(const InstrumentContext& ctx)
        : m_sampleRate(ctx.sampleRate)
        , m_decay(ctx.param("decay"))
        , m_tone(ctx.param("tone"))
        , m_pick(ctx.param("pick"))
        , m_position(ctx.param("position"))
        , m_release(ctx.param("release"))
        , m_body(float(ctx.param("body")))
        , m_width(float(ctx.param("width")))
    {
        m_decay = std::max(0.05, m_decay);
        m_release = std::max(0.005, m_release);
        const int capacity = int(m_sampleRate / LowestHz) + 8;
        for (Voice& v : m_voicePool) {
            v.line.assign(size_t(capacity), 0.0f);
        }
        // Air cavity and the first top plate modes of a small guitar body
        m_resonators[0].setup(105.0, 7.0, m_sampleRate);
        m_resonators[1].setup(215.0, 5.0, m_sampleRate);
        m_resonators[2].setup(420.0, 3.5, m_sampleRate);
        m_dcCoef = float(1.0 - 2.0 * M_PI * 20.0 / m_sampleRate);
    }

    qint64 tailFrames() const override
    {
        // A released string is 75 dB down after 1.25 release times, plus one period
        // of the lowest string that was still in the line, plus the body ring
        const double longestDecay = m_decay * decayScale(Dsp::noteFrequency(LowestPitch));
        const double seconds = 1.25 * std::min(m_release, longestDecay) + 1.0 / LowestHz + 0.1;
        return qint64(seconds * m_sampleRate);
    }

    void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) override
    {
        if (blockStart != m_nextBlock) {
            // Seek: the strings of the old position make no sense here
            for (Voice& v : m_voicePool) {
                v.active = false;
            }
            for (Resonator& r : m_resonators) {
                r.reset();
            }
            m_dcX1 = m_dcY1 = 0.0f;
        }
        m_nextBlock = blockStart + frames;
        const qint64 blockEnd = blockStart + frames;
        m_mid.assign(size_t(frames), 0.0f);
        m_side.assign(size_t(frames), 0.0f);

        for (Voice& v : m_voicePool) {
            v.seen = false;
        }
        for (const RenderNote& note : notes) {
            Voice* v = findVoice(note);
            if (!v) {
                const qint64 from = std::max(blockStart, note.startFrame);
                if (from >= blockEnd || from >= note.startFrame + lifeFrames(note)) {
                    continue;
                }
                v = startVoice(note, from);
                if (!v) {
                    continue;
                }
            }
            v->seen = true;
            renderVoice(*v, blockStart, frames);
        }
        for (Voice& v : m_voicePool) {
            // Notes the engine no longer sends (deleted, moved) stop with them
            if (!v.seen || blockEnd - v.startFrame >= v.lifeFrames) {
                v.active = false;
            }
        }

        const float bodyAmount = m_body * 1.6f;
        for (qint64 f = 0; f < frames; ++f) {
            float m = m_mid[size_t(f)];
            const float body = m_resonators[0].process(m) + 0.7f * m_resonators[1].process(m)
                               + 0.45f * m_resonators[2].process(m);
            m = m * (1.0f - 0.35f * m_body) + body * bodyAmount;
            // DC blocker: the loop filter passes DC, the excitation is only nearly zero-mean
            const float y = m - m_dcX1 + m_dcCoef * m_dcY1;
            m_dcX1 = m;
            m_dcY1 = y;
            const float s = m_side[size_t(f)];
            bus[f * 2] += y + s;
            bus[f * 2 + 1] += y - s;
        }
    }

private:
    // Low strings ring a little longer than high ones, as on a real instrument
    static double decayScale(double hz)
    {
        return std::pow(261.63 / hz, 0.2);
    }

    double frequency(int pitch) const
    {
        return std::min(Dsp::noteFrequency(std::max(pitch, LowestPitch)), m_sampleRate * 0.2);
    }

    double decayFrames(int pitch) const
    {
        return m_decay * decayScale(frequency(pitch)) * m_sampleRate;
    }

    qint64 lifeFrames(const RenderNote& note) const
    {
        const double period = m_sampleRate / frequency(note.pitch);
        const double silent = SilentDb / -60.0;
        const double held = decayFrames(note.pitch) * silent;
        const double released = std::max<qint64>(0, note.endFrame - note.startFrame)
                                + m_release * m_sampleRate * silent + period + 2.0;
        return qint64(std::min(held, released));
    }

    Voice* findVoice(const RenderNote& note)
    {
        for (Voice& v : m_voicePool) {
            if (v.active && v.startFrame == note.startFrame && v.pitch == note.pitch) {
                return &v;
            }
        }
        return nullptr;
    }

    Voice* startVoice(const RenderNote& note, qint64 from)
    {
        // Started after the note on (seek): pluck at the level the string would have by now
        const qint64 elapsed = from - note.startFrame;
        const qint64 noteFrames = std::max<qint64>(0, note.endFrame - note.startFrame);
        double levelDb = -60.0 * elapsed / decayFrames(note.pitch);
        if (elapsed > noteFrames) {
            levelDb += -60.0 * (elapsed - noteFrames) / (m_release * m_sampleRate);
        }
        if (levelDb < SilentDb + 10.0) {
            return nullptr;
        }

        Voice* v = nullptr;
        for (Voice& candidate : m_voicePool) {
            if (!candidate.active) {
                v = &candidate;
                break;
            }
        }
        if (!v) {
            // Pool full: steal the oldest string
            v = &m_voicePool[0];
            for (Voice& candidate : m_voicePool) {
                if (candidate.startFrame < v->startFrame) {
                    v = &candidate;
                }
            }
        }

        const double hz = frequency(note.pitch);
        const double w = 2.0 * M_PI * hz / m_sampleRate;
        const float velocity = qBound(0.0f, note.velocity, 1.0f);

        // Loop damping: the brightness cutoff rises gently with pitch so high notes keep some sparkle
        const double dampHz = 1500.0 * std::pow(2.0, m_tone * 4.0) * std::sqrt(hz / 261.63);
        double b = qBound(0.0, std::exp(-2.0 * M_PI * dampHz / m_sampleRate), 0.95);
        // ... but it may use at most half the decay budget at the fundamental, or high notes
        // (many passes per second) would die long before the decay time whatever the loop gain
        const double passesHeld = decayFrames(note.pitch) / (m_sampleRate / hz);
        b = std::min(b, maxDamping(std::pow(10.0, -1.5 / passesHeld), w));
        const double lpDelay = std::atan2(b * std::sin(w), 1.0 - b * std::cos(w)) / w;
        const double lpMagnitude = (1.0 - b) / std::sqrt(1.0 - 2.0 * b * std::cos(w) + b * b);

        // Tuning: integer delay + one-pole phase delay + allpass fraction = one period
        const double target = m_sampleRate / hz - lpDelay;
        const int capacity = int(v->line.size());
        const int length = qBound(2, int(std::floor(target - 0.5)), capacity);
        const double fraction = qBound(0.05, target - length, 1.95);
        const double c = allpassCoefficient(fraction, w);

        // Loop gain for a -60 dB decay of the fundamental over the decay time, whatever the pitch
        const double passesReleased = m_release * hz;
        const double held = std::pow(10.0, -3.0 / passesHeld) / lpMagnitude;
        const double releasedExtra = std::pow(10.0, -3.0 / std::max(passesReleased, 0.01));

        v->active = true;
        v->startFrame = note.startFrame;
        v->pitch = note.pitch;
        v->length = length;
        v->pos = 0;
        v->sideTap = std::max(1, int(length * 0.37));
        v->apCoef = float(c);
        v->apX1 = v->apY1 = 0.0f;
        v->lpCoef = float(b);
        v->lpY1 = 0.0f;
        v->heldGain = float(std::min(held, 0.99995));
        v->releasedGain = float(std::min(held * releasedExtra, 0.99995));
        v->noteFrames = noteFrames;
        v->lifeFrames = lifeFrames(note);
        v->gain = float(VoiceGain * velocity * std::pow(10.0, levelDb / 20.0));
        v->fadeIn = elapsed > 0 ? m_sampleRate / 200 : 0;
        v->fadeDone = 0;
        excite(*v, velocity);
        return v;
    }

    // Largest one-pole coefficient whose gain at w is still at least `magnitude`
    static double maxDamping(double magnitude, double w)
    {
        const double m2 = magnitude * magnitude;
        if (m2 >= 1.0) {
            return 0.0;
        }
        const double k = 1.0 - m2 * std::cos(w);
        const double q = 1.0 - m2;
        return std::max(0.0, (k - std::sqrt(std::max(0.0, k * k - q * q))) / q);
    }

    // First-order allpass with `delay` samples of phase delay at w (fixed-point refined, since the low-frequency formula drifts for high notes)
    static double allpassCoefficient(double delay, double w)
    {
        double d = delay;
        double c = 0.0;
        for (int i = 0; i < 4; ++i) {
            c = (1.0 - d) / (1.0 + d);
            const double phase = std::atan2(-std::sin(w), c + std::cos(w))
                                 - std::atan2(-c * std::sin(w), 1.0 + c * std::cos(w));
            const double actual = -phase / w;
            d = qBound(0.02, d + (delay - actual), 1.98);
        }
        return (1.0 - d) / (1.0 + d);
    }

    // Fills the delay line with a noise burst: low-passed by the pick (harder with velocity),
    // combed by the pick position, zero-mean, unit RMS
    void excite(Voice& v, float velocity) const
    {
        Dsp::Noise noise(uint32_t(v.startFrame * 2654435761u) ^ uint32_t(v.pitch * 40503u));
        const double pickHz = 400.0 * std::pow(2.0, 6.0 * m_pick * (0.45 + 0.55 * velocity));
        const float a = float(std::exp(-2.0 * M_PI * std::min(pickHz, m_sampleRate * 0.45) / m_sampleRate));
        float* line = v.line.data();
        float s1 = 0.0f;
        float s2 = 0.0f;
        for (int i = 0; i < v.length; ++i) {
            s1 = (1.0f - a) * noise.next() + a * s1;
            s2 = (1.0f - a) * s1 + a * s2;
            line[i] = s2;
        }
        const int offset = qBound(1, int(std::lround(m_position * v.length)), v.length - 1);
        for (int i = v.length - 1; i >= offset; --i) {
            line[i] -= line[i - offset];
        }
        double mean = 0.0;
        for (int i = 0; i < v.length; ++i) {
            mean += line[i];
        }
        mean /= v.length;
        double energy = 0.0;
        for (int i = 0; i < v.length; ++i) {
            line[i] -= float(mean);
            energy += double(line[i]) * line[i];
        }
        const float scale = energy > 1e-12 ? float(1.0 / std::sqrt(energy / v.length)) : 0.0f;
        for (int i = 0; i < v.length; ++i) {
            line[i] *= scale;
        }
    }

    void renderVoice(Voice& v, qint64 blockStart, qint64 frames)
    {
        const qint64 from = std::max(blockStart, v.startFrame);
        const qint64 to = std::min(blockStart + frames, v.startFrame + v.lifeFrames);
        float* line = v.line.data();
        const float c = v.apCoef;
        const float b = v.lpCoef;
        const float side = 0.35f * m_width;
        for (qint64 f = from; f < to; ++f) {
            const qint64 t = f - v.startFrame;
            const float x = line[v.pos];
            int tap = v.pos + v.sideTap;
            if (tap >= v.length) {
                tap -= v.length;
            }
            const float other = line[tap];

            const float ap = c * x + v.apX1 - c * v.apY1;
            v.apX1 = x;
            v.apY1 = ap;
            v.lpY1 = (1.0f - b) * ap + b * v.lpY1;
            line[v.pos] = v.lpY1 * (t < v.noteFrames ? v.heldGain : v.releasedGain);
            if (++v.pos >= v.length) {
                v.pos = 0;
            }

            float g = v.gain;
            if (v.fadeDone < v.fadeIn) {
                g *= float(v.fadeDone) / v.fadeIn;
                ++v.fadeDone;
            }
            m_mid[size_t(f - blockStart)] += x * g;
            m_side[size_t(f - blockStart)] += (x - other) * g * side;
        }
    }

    int m_sampleRate;
    double m_decay;
    double m_tone;
    double m_pick;
    double m_position;
    double m_release;
    float m_body;
    float m_width;

    Voice m_voicePool[MaxVoices];
    Resonator m_resonators[3];
    float m_dcCoef = 0.0f;
    float m_dcX1 = 0.0f;
    float m_dcY1 = 0.0f;
    qint64 m_nextBlock = -1;
    std::vector<float> m_mid;
    std::vector<float> m_side;
};

InstrumentDefinition makeDefinition()
{
    InstrumentDefinition d;
    d.id = "pluck";
    d.name = "Plucked Strings";
    d.description = "A physically modelled plucked string, from nylon guitar to harp and muted bass.";
    d.sortOrder = 30;
    d.params = {
        InstrumentParam::continuous("decay", "Decay", 0.2, 15.0, 2.5, "s", 1, 1, true),
        InstrumentParam::continuous("tone", "Brightness", 0.0, 1.0, 0.45, "%", 100),
        InstrumentParam::continuous("pick", "Pick", 0.0, 1.0, 0.45, "%", 100),
        InstrumentParam::continuous("position", "Pick Position", 0.03, 0.5, 0.14, "%", 100),
        InstrumentParam::continuous("release", "Release", 0.01, 3.0, 0.15, "ms", 1000, 0, true),
        InstrumentParam::continuous("body", "Body", 0.0, 1.0, 0.35, "%", 100),
        InstrumentParam::continuous("width", "Width", 0.0, 1.0, 0.3, "%", 100),
    };
    d.create = [](const InstrumentContext& ctx) { return std::make_unique<Pluck>(ctx); };
    return d;
}

} // namespace

REGISTER_INSTRUMENT(makeDefinition())

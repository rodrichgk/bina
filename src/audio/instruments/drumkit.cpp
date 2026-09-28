// Drum Kit: synthesized one-shot drums mapped like General MIDI (36 = kick, 38 = snare...).
// Pitches outside 35..59 fold into 36..59 by octaves, so every key plays a drum.
//
// Each drum is a recipe mixing up to five sources: a swept sine body, a second sine
// (overtone), filtered noise (optionally as a burst train for claps), "metal" (six
// detuned squares, as in the 808 hats and cymbals) and a short click. Voices keep
// filter state, keyed by (startFrame, pitch); after a seek they are re-run silently
// from their note start, so a hit sounds the same wherever playback starts.

#include "dsp.h"
#include "instrumentapi.h"

#include <algorithm>
#include <array>

namespace {

enum Kit { Kit808, Kit909, KitAcoustic };
enum FilterMode { LowPass, BandPass, HighPass };
enum HatKind { ClosedHat, PedalHat, OpenHat };
enum CymbalKind { Crash, Crash2, China, Splash };
enum RideKind { Ride, Ride2, RideBell };

constexpr int FirstPitch = 35;
constexpr int SlotCount = 25; // pitches 35..59
constexpr int MaxVoices = 48;
constexpr double Ln1000 = 6.907755; // turns a -60 dB decay time into a time constant
constexpr double PeakLevel = 11000.0; // full-velocity peak of a level-1 drum (about -9.5 dBFS)

// The 808's six hi-hat/cymbal square oscillators; the cowbell uses the last two
constexpr double MetalHz[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};

struct Settings {
    int kit = Kit909;
    double tune = 0.0;
    double kickDecay = 0.6;
    double punch = 0.5;
    double snappy = 0.5;
    double hatDecay = 0.5;
    double tone = 0.5;
    double drive = 0.0;
};

// Times are seconds to -60 dB, levels are relative inside the recipe (the whole
// recipe is normalized afterwards), frequencies are Hz before Tune/Tone.
struct Recipe {
    double attack = 0.0;
    // Sine body sweeping down from bodyHz * sweep to bodyHz; the overtone follows the sweep
    double bodyHz = 100, sweep = 1, sweepTime = 0.01, bodyDecay = 0.1, bodyLevel = 0;
    double partialHz = 100, partialDecay = 0.1, partialLevel = 0;
    FilterMode noiseMode = BandPass;
    double noiseHz = 1000, noiseQ = 0.7, noiseDecay = 0.1, noiseLevel = 0;
    int bursts = 1;
    double burstGap = 0.01, burstDecay = 0.004;
    int metalFirst = 0; // 0 = six squares, 4 = two (cowbell)
    double metalScale = 1, metalHz = 8000, metalQ = 1, metalHighPass = 5000;
    double metalDecay = 0.1, metalLevel = 0, metalStrike = 0, metalStrikeDecay = 0.03;
    double clickHz = 4000, clickDecay = 0.003, clickLevel = 0;
    double level = 1, pan = 0;
    bool chokable = false; // open hat
    bool chokes = false;   // closed and pedal hat

    // Derived per sample rate
    double bodyInc = 0, partialInc = 0, metalInc[6] = {};
    double sweepMul = 0, bodyMul = 0, partialMul = 0, noiseMul = 0, burstMul = 0;
    double metalMul = 0, strikeMul = 0, clickMul = 0;
    qint64 attackFrames = 0, gapFrames = 1, lifetime = 1, fadeStart = 0;
    double norm = 1.0;
};

// Topology-preserving state-variable filter (stable for any cutoff below Nyquist)
class Svf
{
public:
    void set(double hz, double q, int sampleRate)
    {
        hz = qBound(20.0, hz, sampleRate * 0.45);
        const double g = std::tan(M_PI * hz / sampleRate);
        m_k = 1.0 / std::max(0.1, q);
        m_a1 = 1.0 / (1.0 + g * (g + m_k));
        m_a2 = g * m_a1;
        m_a3 = g * m_a2;
        m_ic1 = m_ic2 = 0.0;
    }
    double process(double x, FilterMode mode)
    {
        const double v3 = x - m_ic2;
        const double v1 = m_a1 * m_ic1 + m_a2 * v3;
        const double v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
        m_ic1 = 2.0 * v1 - m_ic1;
        m_ic2 = 2.0 * v2 - m_ic2;
        switch (mode) {
        case LowPass:
            return v2;
        case BandPass:
            return v1;
        case HighPass:
        default:
            return x - m_k * v1 - v2;
        }
    }

private:
    double m_k = 1, m_a1 = 0, m_a2 = 0, m_a3 = 0;
    double m_ic1 = 0, m_ic2 = 0;
};

struct Voice {
    bool active = false;
    bool finished = false;
    qint64 start = 0;
    int pitch = 0;
    qint64 seenBlock = -1;
    const Recipe* recipe = nullptr;
    qint64 t = 0;
    qint64 chokeAt = -1; // frames after start, -1 = not choked
    double chokeEnv = 1.0;
    float gainL = 0, gainR = 0;
    Dsp::Noise noise;
    double bodyPhase = 0, partialPhase = 0, metalPhase[6] = {};
    double sweepEnv = 1, bodyEnv = 1, partialEnv = 1, noiseEnv = 1, burstEnv = 1;
    double metalEnv = 1, strikeEnv = 1, clickEnv = 1;
    Svf noiseFilter, metalBand, metalHigh, clickFilter;

    void init(const Recipe& r, qint64 startFrame, int notePitch, float velocity, int sampleRate)
    {
        active = true;
        finished = false;
        start = startFrame;
        pitch = notePitch;
        recipe = &r;
        t = 0;
        chokeAt = -1;
        chokeEnv = 1.0;
        const uint32_t seed = uint32_t(startFrame) * 2654435761u ^ uint32_t(notePitch + 1) * 40503u ^ 0x9e3779b9u;
        noise = Dsp::Noise(seed);
        bodyPhase = partialPhase = 0.0;
        // Free-running oscillators in hardware: start the squares at scattered phases
        for (double& p : metalPhase) {
            p = 0.5 + 0.5 * noise.next();
        }
        sweepEnv = bodyEnv = partialEnv = noiseEnv = burstEnv = 1.0;
        metalEnv = strikeEnv = clickEnv = 1.0;
        // Softer hits are a little darker
        const double bright = std::pow(2.0, (qBound(0.0f, velocity, 1.0f) - 1.0) * 0.6);
        noiseFilter.set(r.noiseHz * bright, r.noiseQ, sampleRate);
        metalBand.set(r.metalHz * bright, r.metalQ, sampleRate);
        metalHigh.set(r.metalHighPass * bright, 0.7, sampleRate);
        clickFilter.set(r.clickHz * bright, 0.7, sampleRate);
    }

    // Raw mono mix of all sources, before normalization
    double next()
    {
        const Recipe& r = *recipe;
        double out = 0.0;
        const double sweep = 1.0 + (r.sweep - 1.0) * sweepEnv;
        sweepEnv *= r.sweepMul;
        if (r.bodyLevel > 0.0) {
            bodyPhase += r.bodyInc * sweep;
            bodyPhase -= std::floor(bodyPhase);
            out += std::sin(2.0 * M_PI * bodyPhase) * bodyEnv * r.bodyLevel;
            bodyEnv *= r.bodyMul;
        }
        if (r.partialLevel > 0.0) {
            partialPhase += r.partialInc * sweep;
            partialPhase -= std::floor(partialPhase);
            out += std::sin(2.0 * M_PI * partialPhase) * partialEnv * r.partialLevel;
            partialEnv *= r.partialMul;
        }
        const double white = noise.next();
        if (r.noiseLevel > 0.0) {
            double env;
            if (r.bursts > 1 && t < (r.bursts - 1) * r.gapFrames) {
                if (t % r.gapFrames == 0) {
                    burstEnv = 1.0;
                }
                env = burstEnv;
                burstEnv *= r.burstMul;
            } else {
                env = noiseEnv;
                noiseEnv *= r.noiseMul;
            }
            out += noiseFilter.process(white, r.noiseMode) * env * r.noiseLevel;
        }
        if (r.metalLevel > 0.0) {
            double m = 0.0;
            for (int i = r.metalFirst; i < 6; ++i) {
                double& p = metalPhase[i];
                p += r.metalInc[i];
                p -= std::floor(p);
                m += (p < 0.5 ? 1.0 : -1.0) + Dsp::polyBlep(p, r.metalInc[i])
                     - Dsp::polyBlep(std::fmod(p + 0.5, 1.0), r.metalInc[i]);
            }
            m = metalHigh.process(metalBand.process(m, BandPass), HighPass);
            out += m * (strikeEnv * r.metalStrike + metalEnv * (1.0 - r.metalStrike)) * r.metalLevel;
            metalEnv *= r.metalMul;
            strikeEnv *= r.strikeMul;
        }
        if (r.clickLevel > 0.0) {
            out += clickFilter.process(white, LowPass) * clickEnv * r.clickLevel;
            clickEnv *= r.clickMul;
        }
        if (t < r.attackFrames) {
            out *= double(t) / r.attackFrames;
        }
        return out;
    }
};

double decayMul(double seconds, int sampleRate)
{
    return std::exp(-Ln1000 / (std::max(0.0005, seconds) * sampleRate));
}

// ---- Recipes ---------------------------------------------------------------

Recipe kick(const Settings& s, bool alt)
{
    Recipe r;
    const double punch = s.punch * (alt ? 0.6 : 1.0);
    r.bodyLevel = 1.0;
    r.bodyDecay = s.kickDecay * (alt ? 1.3 : 1.0);
    switch (s.kit) {
    case Kit808:
        r.bodyHz = 49;
        r.sweep = 1.0 + 1.5 * punch;
        r.sweepTime = 0.1;
        r.clickLevel = 0.15 * punch;
        r.clickHz = 2500;
        r.clickDecay = 0.004;
        break;
    case Kit909:
        r.bodyHz = 54;
        r.sweep = 1.0 + 5.0 * punch;
        r.sweepTime = 0.05;
        r.bodyDecay *= 0.8;
        r.clickLevel = 0.6 * punch;
        r.clickHz = 6000;
        r.clickDecay = 0.006;
        break;
    default:
        // Shorter, with a beater click, a shell overtone and a bit of low skin noise
        r.bodyHz = 62;
        r.sweep = 1.0 + 1.2 * punch;
        r.sweepTime = 0.035;
        r.bodyDecay *= 0.55;
        r.partialHz = 62 * 1.58;
        r.partialDecay = r.bodyDecay * 0.4;
        r.partialLevel = 0.25;
        r.noiseMode = LowPass;
        r.noiseHz = 600;
        r.noiseDecay = 0.12;
        r.noiseLevel = 0.3;
        r.clickLevel = 0.1 + 0.5 * punch;
        r.clickHz = 3000;
        r.clickDecay = 0.01;
        break;
    }
    if (alt) {
        r.bodyHz *= 0.84;
        r.partialHz *= 0.84;
    }
    r.level = 1.0;
    return r;
}

Recipe snare(const Settings& s, bool alt)
{
    Recipe r;
    r.sweepTime = 0.03;
    switch (s.kit) {
    case Kit808:
        r.bodyHz = 180;
        r.sweep = 1.1;
        r.bodyDecay = 0.22;
        r.partialHz = 330;
        r.partialDecay = 0.14;
        r.partialLevel = 0.6;
        r.noiseHz = 6000;
        r.noiseQ = 0.5;
        r.noiseDecay = 0.28;
        break;
    case Kit909:
        r.bodyHz = 190;
        r.sweep = 1.5;
        r.bodyDecay = 0.18;
        r.partialHz = 340;
        r.partialDecay = 0.12;
        r.partialLevel = 0.5;
        r.noiseHz = 7000;
        r.noiseQ = 0.5;
        r.noiseDecay = 0.32;
        r.clickLevel = 0.3;
        r.clickHz = 5000;
        break;
    default:
        r.bodyHz = 200;
        r.sweep = 1.15;
        r.bodyDecay = 0.2;
        r.partialHz = 330;
        r.partialDecay = 0.15;
        r.partialLevel = 0.4;
        r.noiseHz = 4500;
        r.noiseQ = 0.55;
        r.noiseDecay = 0.38;
        r.clickLevel = 0.4;
        r.clickHz = 3500;
        r.clickDecay = 0.008;
        break;
    }
    // Snappy trades the drum body for snare wires
    r.bodyLevel = 1.0 - 0.5 * s.snappy;
    r.partialLevel *= r.bodyLevel;
    r.noiseLevel = 0.6 + 2.2 * s.snappy;
    r.noiseDecay *= 0.6 + 0.8 * s.snappy;
    if (alt) {
        r.bodyHz *= 1.22;
        r.partialHz *= 1.22;
        r.bodyDecay *= 0.75;
        r.partialDecay *= 0.75;
        r.noiseDecay *= 0.75;
        r.noiseHz *= 1.3;
    }
    r.level = 0.9;
    return r;
}

Recipe clap(const Settings& s)
{
    Recipe r;
    r.noiseLevel = 1.0;
    switch (s.kit) {
    case Kit808:
        r.noiseHz = 1100;
        r.noiseQ = 1.5;
        r.bursts = 4;
        r.burstGap = 0.010;
        r.noiseDecay = 0.3;
        break;
    case Kit909:
        r.noiseHz = 1300;
        r.noiseQ = 1.2;
        r.bursts = 4;
        r.burstGap = 0.009;
        r.burstDecay = 0.003;
        r.noiseDecay = 0.25;
        break;
    default:
        r.noiseHz = 1600;
        r.noiseQ = 0.9;
        r.bursts = 5;
        r.burstGap = 0.008;
        r.noiseDecay = 0.2;
        break;
    }
    r.level = 0.85;
    return r;
}

Recipe rim(const Settings& s)
{
    Recipe r;
    r.bodyLevel = 1.0;
    switch (s.kit) {
    case Kit808:
        r.bodyHz = 1700;
        r.bodyDecay = 0.035;
        r.partialHz = 470;
        r.partialDecay = 0.05;
        r.partialLevel = 0.7;
        r.clickLevel = 0.5;
        r.clickHz = 8000;
        r.clickDecay = 0.003;
        break;
    case Kit909:
        r.bodyHz = 1550;
        r.bodyDecay = 0.04;
        r.partialHz = 520;
        r.partialDecay = 0.05;
        r.partialLevel = 0.6;
        r.noiseHz = 3000;
        r.noiseQ = 1.0;
        r.noiseDecay = 0.03;
        r.noiseLevel = 0.4;
        r.clickLevel = 0.4;
        r.clickHz = 7000;
        break;
    default:
        // Cross-stick: woody knock plus the stick on the rim
        r.bodyHz = 900;
        r.bodyDecay = 0.03;
        r.partialHz = 2100;
        r.partialDecay = 0.02;
        r.partialLevel = 0.5;
        r.noiseHz = 2500;
        r.noiseQ = 1.2;
        r.noiseDecay = 0.05;
        r.noiseLevel = 0.6;
        r.clickLevel = 0.6;
        r.clickHz = 6000;
        break;
    }
    r.level = 0.7;
    r.pan = -0.1;
    return r;
}

Recipe tom(const Settings& s, double hz, double pan)
{
    Recipe r;
    r.bodyHz = hz;
    r.bodyLevel = 1.0;
    // Lower toms ring longer
    const double ring = std::pow(110.0 / hz, 0.4);
    switch (s.kit) {
    case Kit808:
        r.sweep = 1.25;
        r.sweepTime = 0.3;
        r.bodyDecay = 0.55 * ring;
        r.noiseMode = LowPass;
        r.noiseHz = 1500;
        r.noiseDecay = 0.08;
        r.noiseLevel = 0.12;
        break;
    case Kit909:
        r.sweep = 1.7;
        r.sweepTime = 0.09;
        r.bodyDecay = 0.45 * ring;
        r.noiseMode = LowPass;
        r.noiseHz = 3000;
        r.noiseDecay = 0.06;
        r.noiseLevel = 0.1;
        r.clickLevel = 0.35;
        r.clickHz = 5000;
        break;
    default:
        r.sweep = 1.12;
        r.sweepTime = 0.15;
        r.bodyDecay = 0.7 * ring;
        r.partialHz = hz * 1.5;
        r.partialDecay = r.bodyDecay * 0.4;
        r.partialLevel = 0.35;
        r.noiseMode = LowPass;
        r.noiseHz = 1500;
        r.noiseDecay = 0.1;
        r.noiseLevel = 0.15;
        r.clickLevel = 0.4;
        r.clickHz = 3000;
        r.clickDecay = 0.008;
        break;
    }
    r.level = 0.75;
    r.pan = pan;
    return r;
}

Recipe hat(const Settings& s, HatKind kind)
{
    Recipe r;
    const double closedDecay = 0.05 + 0.1 * s.hatDecay;
    const double decay = kind == OpenHat ? s.hatDecay : kind == ClosedHat ? closedDecay : closedDecay * 0.7;
    r.noiseMode = HighPass;
    switch (s.kit) {
    case Kit808:
        r.metalLevel = 1.0;
        r.metalHz = 10000;
        r.metalHighPass = 7000;
        r.noiseHz = 8000;
        r.noiseLevel = 0.15;
        break;
    case Kit909:
        r.metalLevel = 0.6;
        r.metalScale = 1.15;
        r.metalHz = 11000;
        r.metalHighPass = 7500;
        r.noiseHz = 7500;
        r.noiseLevel = 0.8;
        break;
    default:
        r.metalLevel = 0.5;
        r.metalScale = 1.41;
        r.metalHz = 9000;
        r.metalQ = 0.8;
        r.metalHighPass = 6000;
        r.noiseHz = 6000;
        r.noiseLevel = 0.9;
        break;
    }
    r.metalDecay = decay;
    r.noiseDecay = decay * 0.9;
    r.level = 0.65;
    r.pan = 0.2;
    if (kind == OpenHat) {
        r.metalStrike = 0.3;
        r.chokable = true;
    } else {
        r.chokes = true;
    }
    if (kind == PedalHat) {
        r.attack = 0.002;
        r.metalHz *= 0.8;
        r.noiseHz *= 0.8;
        r.level = 0.55;
    }
    return r;
}

Recipe cymbal(const Settings& s, CymbalKind kind)
{
    Recipe r;
    r.noiseMode = HighPass;
    r.noiseHz = 4000;
    r.noiseLevel = 1.0;
    r.metalScale = 1.7;
    r.metalHz = 7000;
    r.metalQ = 0.7;
    r.metalHighPass = 3000;
    r.metalStrike = 0.5;
    r.metalStrikeDecay = 0.05;
    switch (s.kit) {
    case Kit808:
        r.metalLevel = 0.6;
        r.metalDecay = 1.6;
        break;
    case Kit909:
        r.metalLevel = 0.4;
        r.metalDecay = 2.0;
        break;
    default:
        r.metalLevel = 0.7;
        r.metalScale = 2.3;
        r.metalDecay = 2.4;
        break;
    }
    r.pan = -0.25;
    switch (kind) {
    case Crash2:
        r.metalDecay *= 0.85;
        r.metalScale *= 0.85;
        r.metalHz *= 0.85;
        r.noiseHz *= 0.85;
        r.pan = 0.3;
        break;
    case China:
        r.metalScale = 1.3;
        r.metalHz = 5000;
        r.metalQ = 1.5;
        r.metalLevel = 1.0;
        r.noiseLevel = 0.5;
        r.metalDecay *= 0.8;
        r.pan = 0.35;
        break;
    case Splash:
        r.metalDecay *= 0.45;
        r.metalScale *= 1.25;
        r.metalHz *= 1.25;
        r.noiseHz *= 1.25;
        r.pan = -0.35;
        break;
    case Crash:
    default:
        break;
    }
    r.noiseDecay = r.metalDecay * 0.9;
    r.level = 0.55;
    return r;
}

Recipe ride(const Settings& s, RideKind kind)
{
    Recipe r;
    const double decay = s.kit == Kit808 ? 2.5 : s.kit == Kit909 ? 2.8 : 3.0;
    r.metalLevel = 1.0;
    r.metalScale = 1.25;
    r.metalHz = 5500;
    r.metalQ = 1.3;
    r.metalHighPass = 2500;
    r.metalDecay = decay;
    r.metalStrike = 0.4;
    r.noiseMode = HighPass;
    r.noiseHz = 7000;
    r.noiseLevel = s.kit == KitAcoustic ? 0.5 : 0.3;
    r.noiseDecay = decay * 0.5;
    r.clickLevel = 0.3;
    r.clickHz = 6000;
    r.pan = 0.3;
    r.level = 0.55;
    if (kind == Ride2) {
        r.metalScale = 1.12;
        r.metalHz *= 0.9;
        r.metalDecay *= 0.9;
        r.pan = 0.4;
    } else if (kind == RideBell) {
        r.metalScale = 2.2;
        r.metalHz = 3200;
        r.metalQ = 2.0;
        r.metalHighPass = 1500;
        r.metalDecay = decay * 0.6;
        r.partialHz = 1650;
        r.partialDecay = decay * 0.5;
        r.partialLevel = 0.5;
        r.noiseLevel = 0.1;
        r.level = 0.6;
    }
    return r;
}

Recipe tambourine(const Settings& s)
{
    Recipe r;
    r.metalLevel = 0.7;
    r.metalScale = s.kit == KitAcoustic ? 5.2 : 6.0;
    r.metalHz = 9000;
    r.metalHighPass = 6000;
    r.metalDecay = 0.25;
    r.noiseMode = HighPass;
    r.noiseHz = 7000;
    r.noiseLevel = 0.8;
    r.bursts = 2;
    r.burstGap = 0.018;
    r.burstDecay = 0.01;
    r.noiseDecay = 0.25;
    r.level = 0.5;
    r.pan = -0.3;
    return r;
}

Recipe cowbell(const Settings& s)
{
    Recipe r;
    r.metalFirst = 4;
    r.metalLevel = 1.0;
    r.metalHz = 900;
    r.metalQ = 1.2;
    r.metalHighPass = 350;
    r.metalStrike = 0.7;
    r.metalStrikeDecay = 0.06;
    r.metalDecay = s.kit == KitAcoustic ? 0.35 : 0.45;
    if (s.kit == KitAcoustic) {
        r.metalScale = 1.06;
        r.noiseHz = 2500;
        r.noiseDecay = 0.05;
        r.noiseLevel = 0.15;
        r.clickLevel = 0.3;
    }
    r.level = 0.6;
    r.pan = 0.15;
    return r;
}

Recipe shaker()
{
    Recipe r;
    r.attack = 0.018;
    r.noiseHz = 6500;
    r.noiseQ = 1.2;
    r.noiseDecay = 0.14;
    r.noiseLevel = 1.0;
    r.level = 0.45;
    r.pan = 0.25;
    return r;
}

// GM-style layout of 35..59
Recipe recipeForPitch(int pitch, const Settings& s)
{
    switch (pitch) {
    case 35: return kick(s, true);
    case 36: return kick(s, false);
    case 37: return rim(s);
    case 38: return snare(s, false);
    case 39: return clap(s);
    case 40: return snare(s, true);
    case 41: return tom(s, 82, -0.3);
    case 42: return hat(s, ClosedHat);
    case 43: return tom(s, 98, -0.2);
    case 44: return hat(s, PedalHat);
    case 45: return tom(s, 116, -0.1);
    case 46: return hat(s, OpenHat);
    case 47: return tom(s, 138, 0.05);
    case 48: return tom(s, 164, 0.15);
    case 49: return cymbal(s, Crash);
    case 50: return tom(s, 196, 0.25);
    case 51: return ride(s, Ride);
    case 52: return cymbal(s, China);
    case 53: return ride(s, RideBell);
    case 54: return tambourine(s);
    case 55: return cymbal(s, Splash);
    case 56: return cowbell(s);
    case 57: return cymbal(s, Crash2);
    case 58: return shaker();
    case 59:
    default: return ride(s, Ride2);
    }
}

int slotForPitch(int pitch)
{
    if (pitch == FirstPitch) {
        return 0;
    }
    if (pitch < 36) {
        pitch = 36 + ((pitch - 36) % 12 + 12) % 12;
    } else if (pitch > 59) {
        pitch = 48 + (pitch - 48) % 12;
    }
    return pitch - FirstPitch;
}

// Applies Tune and Tone, then precomputes per-sample constants and the lifetime
void finalize(Recipe& r, const Settings& s, int sampleRate)
{
    const double tuneMul = std::pow(2.0, s.tune / 12.0);
    const double filterMul = tuneMul * std::pow(2.0, (s.tone - 0.5) * 2.0);
    r.bodyHz *= tuneMul;
    r.partialHz *= tuneMul;
    r.metalScale *= tuneMul;
    r.noiseHz *= filterMul;
    r.metalHz *= filterMul;
    r.metalHighPass *= filterMul;
    r.clickHz *= filterMul;

    const double nyquistSafe = sampleRate * 0.45;
    r.bodyInc = std::min(r.bodyHz, nyquistSafe) / sampleRate;
    r.partialInc = std::min(r.partialHz, nyquistSafe) / sampleRate;
    for (int i = 0; i < 6; ++i) {
        r.metalInc[i] = std::min(MetalHz[i] * r.metalScale, nyquistSafe) / sampleRate;
    }
    // sweepTime = time to settle within about 5% of the final pitch
    r.sweepMul = std::exp(-3.0 / (std::max(0.001, r.sweepTime) * sampleRate));
    r.bodyMul = decayMul(r.bodyDecay, sampleRate);
    r.partialMul = decayMul(r.partialDecay, sampleRate);
    r.noiseMul = decayMul(r.noiseDecay, sampleRate);
    r.burstMul = decayMul(r.burstDecay, sampleRate);
    r.metalMul = decayMul(r.metalDecay, sampleRate);
    r.strikeMul = decayMul(r.metalStrikeDecay, sampleRate);
    r.clickMul = decayMul(r.clickDecay, sampleRate);
    r.attackFrames = qint64(r.attack * sampleRate);
    r.gapFrames = std::max<qint64>(1, qint64(r.burstGap * sampleRate));

    double longest = 0.01;
    if (r.bodyLevel > 0.0) {
        longest = std::max(longest, r.bodyDecay);
    }
    if (r.partialLevel > 0.0) {
        longest = std::max(longest, r.partialDecay);
    }
    if (r.noiseLevel > 0.0) {
        longest = std::max(longest, (r.bursts - 1) * r.burstGap + r.noiseDecay);
    }
    if (r.metalLevel > 0.0) {
        longest = std::max({longest, r.metalDecay, r.metalStrikeDecay});
    }
    if (r.clickLevel > 0.0) {
        longest = std::max(longest, r.clickDecay);
    }
    r.lifetime = r.attackFrames + qint64(longest * sampleRate) + 1;
    // Last 20% is already below -48 dB; a linear fade there guarantees true silence at the end
    r.fadeStart = r.lifetime - r.lifetime / 5;
}

class DrumKit : public Instrument
{
public:
    explicit DrumKit(const InstrumentContext& ctx)
        : m_sampleRate(ctx.sampleRate)
    {
        m_settings.kit = qBound(0, ctx.choice("kit"), 2);
        m_settings.tune = qBound(-12.0, ctx.param("tune"), 12.0);
        m_settings.kickDecay = qBound(0.1, ctx.param("kickDecay"), 2.0);
        m_settings.punch = qBound(0.0, ctx.param("punch"), 1.0);
        m_settings.snappy = qBound(0.0, ctx.param("snappy"), 1.0);
        m_settings.hatDecay = qBound(0.1, ctx.param("hatDecay"), 2.0);
        m_settings.tone = qBound(0.0, ctx.param("tone"), 1.0);
        m_settings.drive = qBound(0.0, ctx.param("drive"), 1.0);
        m_driveGain = 1.0 + 7.0 * m_settings.drive;
        m_driveNorm = std::tanh(m_driveGain);
        m_chokeMul = std::exp(-1.0 / (0.008 * m_sampleRate));

        for (int slot = 0; slot < SlotCount; ++slot) {
            Recipe& r = m_recipes[size_t(slot)];
            r = recipeForPitch(FirstPitch + slot, m_settings);
            finalize(r, m_settings, m_sampleRate);
            m_tail = std::max(m_tail, r.lifetime);
            normalize(r, FirstPitch + slot);
        }
    }

    qint64 tailFrames() const override { return m_tail; }

    void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) override
    {
        const qint64 blockEnd = blockStart + frames;
        if (blockStart != m_nextBlock) {
            for (Voice& v : m_voices) {
                v.active = false;
            }
        }
        m_nextBlock = blockEnd;

        for (const RenderNote& note : notes) {
            const Recipe& r = m_recipes[size_t(slotForPitch(note.pitch))];
            if (note.startFrame >= blockEnd || note.startFrame + r.lifetime <= blockStart) {
                continue;
            }
            Voice* v = findVoice(note, blockStart);
            if (!v) {
                v = allocateVoice();
                v->init(r, note.startFrame, note.pitch, note.velocity, m_sampleRate);
                const float amp = float(PeakLevel * r.level * std::pow(qBound(0.0f, note.velocity, 1.0f), 1.4f));
                v->gainL = amp * float(1.0 - std::max(0.0, r.pan));
                v->gainR = amp * float(1.0 + std::min(0.0, r.pan));
            }
            v->seenBlock = blockStart;
            if (r.chokable) {
                updateChoke(*v, notes);
            }
            // Started before this block (seek, or note added late): catch up silently
            while (!v->finished && v->start + v->t < blockStart) {
                sample(*v);
            }
            for (qint64 f = v->start + v->t; f < blockEnd && !v->finished; ++f) {
                const float x = sample(*v);
                bus[(f - blockStart) * 2] += x * v->gainL;
                bus[(f - blockStart) * 2 + 1] += x * v->gainR;
            }
        }

        // Notes that left the list (deleted, moved) stop sounding
        for (Voice& v : m_voices) {
            if (v.active && (v.finished || v.seenBlock != blockStart)) {
                v.active = false;
            }
        }
    }

private:
    // One output sample of a voice at unit gain: normalized, driven, faded, choked
    float sample(Voice& v)
    {
        const Recipe& r = *v.recipe;
        double x = v.next() * r.norm;
        if (m_settings.drive > 0.0) {
            // Saturation fattens the whole decay; trim so driven hits keep the same headroom
            x += m_settings.drive * (std::tanh(x * m_driveGain) / m_driveNorm - x);
            x *= 1.0 - 0.4 * m_settings.drive;
        }
        if (v.t >= r.fadeStart) {
            x *= double(r.lifetime - v.t) / double(r.lifetime - r.fadeStart);
        }
        if (v.chokeAt >= 0 && v.t >= v.chokeAt) {
            x *= v.chokeEnv;
            v.chokeEnv *= m_chokeMul;
            if (v.chokeEnv < 1e-4) {
                v.finished = true;
            }
        }
        if (++v.t >= r.lifetime) {
            v.finished = true;
        }
        return float(x);
    }

    // Scales the recipe so a full-velocity hit peaks at 1 before drive and gain
    void normalize(Recipe& r, int pitch)
    {
        Voice v;
        v.init(r, 0, pitch, 1.0f, m_sampleRate);
        const qint64 frames = std::min<qint64>(r.lifetime, qint64(0.3 * m_sampleRate));
        double peak = 0.0;
        for (qint64 i = 0; i < frames; ++i) {
            peak = std::max(peak, std::abs(v.next()));
            ++v.t;
        }
        r.norm = peak > 1e-6 ? 1.0 / peak : 0.0;
    }

    Voice* findVoice(const RenderNote& note, qint64 blockStart)
    {
        for (Voice& v : m_voices) {
            if (v.active && v.start == note.startFrame && v.pitch == note.pitch && v.seenBlock != blockStart) {
                return &v;
            }
        }
        return nullptr;
    }

    Voice* allocateVoice()
    {
        Voice* oldest = &m_voices[0];
        for (Voice& v : m_voices) {
            if (!v.active) {
                return &v;
            }
            if (v.start < oldest->start) {
                oldest = &v;
            }
        }
        return oldest;
    }

    // A closed or pedal hat hit after an open hat chokes it
    void updateChoke(Voice& v, const std::vector<RenderNote>& notes) const
    {
        for (const RenderNote& other : notes) {
            if (other.startFrame <= v.start || !m_recipes[size_t(slotForPitch(other.pitch))].chokes) {
                continue;
            }
            const qint64 at = other.startFrame - v.start;
            if (v.chokeAt < 0 || at < v.chokeAt) {
                v.chokeAt = at;
            }
        }
    }

    int m_sampleRate;
    Settings m_settings;
    double m_driveGain = 1.0;
    double m_driveNorm = 1.0;
    double m_chokeMul = 0.0;
    qint64 m_tail = 0;
    qint64 m_nextBlock = -1;
    std::array<Recipe, SlotCount> m_recipes;
    std::array<Voice, MaxVoices> m_voices;
};

InstrumentDefinition makeDefinition()
{
    InstrumentDefinition d;
    d.id = "drums";
    d.name = "Drum Kit";
    d.description = "Synthesized drum kit laid out like General MIDI, from C2 kick to cymbals.";
    d.sortOrder = 10;
    d.params = {
        InstrumentParam::choice("kit", "Kit", {"808", "909", "Acoustic-ish"}, Kit909),
        InstrumentParam::continuous("tune", "Tune", -12.0, 12.0, 0.0, "st", 1, 1),
        InstrumentParam::continuous("kickDecay", "Kick Decay", 0.1, 2.0, 0.6, "ms", 1000, 0, true),
        InstrumentParam::continuous("punch", "Kick Punch", 0.0, 1.0, 0.5, "%", 100),
        InstrumentParam::continuous("snappy", "Snappy", 0.0, 1.0, 0.5, "%", 100),
        InstrumentParam::continuous("hatDecay", "Hat Decay", 0.1, 2.0, 0.5, "ms", 1000, 0, true),
        InstrumentParam::continuous("tone", "Tone", 0.0, 1.0, 0.5, "%", 100),
        InstrumentParam::continuous("drive", "Drive", 0.0, 1.0, 0.0, "%", 100),
    };
    d.create = [](const InstrumentContext& ctx) { return std::make_unique<DrumKit>(ctx); };
    return d;
}

} // namespace

REGISTER_INSTRUMENT(makeDefinition())

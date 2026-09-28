#ifndef INSTRUMENTAPI_H
#define INSTRUMENTAPI_H

// Instrument plug-in API.
//
// An instrument is one .cpp file in src/audio/instruments/ that:
//   1. subclasses Instrument (turns notes into audio), and
//   2. registers an InstrumentDefinition (id, name, parameters, factory) with
//      REGISTER_INSTRUMENT(...).
// The build picks the file up automatically; the track settings dialog builds
// controls from the declared parameters; the engine creates instances per track.
//
// Depends on Qt Core only, so instruments can be built and tested standalone
// (see tools/instrument_render.cpp).

#include "../../core/instrumentsettings.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <vector>

// A note placed on the engine's frame timeline
struct RenderNote {
    qint64 startFrame; // note on
    qint64 endFrame;   // note off; the instrument may keep sounding for tailFrames() after it
    int pitch;         // MIDI note number, 60 = C4
    float velocity;    // 0..1
};

// Turns notes into audio. One instance per track, rendered from the audio thread only.
//
// render() is called once per audio block with every note of the track that might
// sound in it. Rules:
//  - Buffers are interleaved stereo float in 16-bit scale (+-32768). ADD into the
//    bus, never overwrite it; aim for peaks around -12 dBFS per voice (about 8000)
//    so chords have headroom.
//  - Blocks normally follow each other (blockStart == previous blockStart + frames),
//    but the transport can seek: blockStart may jump backwards or forwards. Voices
//    computed from "time since note start" handle this for free. If you keep per-voice
//    state (filters, delay lines, phase accumulators), key it by (startFrame, pitch)
//    and reset it when blockStart is not where the previous block ended.
//  - No allocation, locking or I/O per sample; per-block std::vector reuse is fine.
//  - Output must stay finite (no NaN/inf) for every parameter value in range.
class Instrument
{
public:
    virtual ~Instrument() = default;
    virtual void render(float* bus, qint64 blockStart, qint64 frames, const std::vector<RenderNote>& notes) = 0;
    // How long a note can keep sounding after its endFrame (release, decay, ring)
    virtual qint64 tailFrames() const = 0;
};

// One user-facing parameter. The track settings dialog renders it as a slider,
// a dropdown or a checkbox; the value arrives in InstrumentContext::param().
struct InstrumentParam {
    enum class Kind { Continuous, Choice, Toggle };

    QString id;                // stable key stored in projects, e.g. "attack"
    QString label;             // shown in the UI, e.g. "Attack"
    Kind kind = Kind::Continuous;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;

    // Continuous display: shown value = value * displayScale, with `decimals` and `unit`
    // e.g. seconds shown as ms: displayScale 1000, unit "ms"
    double displayScale = 1.0;
    int decimals = 0;
    QString unit;
    bool logarithmic = false;  // slider travels logarithmically (frequencies, times)

    QStringList choices;       // Kind::Choice: value is the index into this list

    static InstrumentParam continuous(const QString& id, const QString& label, double min, double max,
                                      double defaultValue, const QString& unit = {}, double displayScale = 1.0,
                                      int decimals = 0, bool logarithmic = false);
    static InstrumentParam choice(const QString& id, const QString& label, const QStringList& choices,
                                  int defaultIndex = 0);
    static InstrumentParam toggle(const QString& id, const QString& label, bool defaultOn = false);
};

// What a factory gets to build an instance
struct InstrumentContext {
    const InstrumentSettings& settings;
    const struct InstrumentDefinition& definition;
    int sampleRate;

    // The parameter's value, or its default when the track hasn't set it
    double param(const QString& id) const;
    int choice(const QString& id) const { return int(param(id) + 0.5); }
    bool toggle(const QString& id) const { return param(id) >= 0.5; }
};

struct InstrumentDefinition {
    QString id;          // stable, lowercase, e.g. "fm"
    QString name;        // shown in the UI, e.g. "FM Synth"
    QString description; // one short sentence
    int sortOrder = 100; // position in the instrument list
    QVector<InstrumentParam> params;
    bool usesSample = false; // shows "Load Sample..." in the settings
    std::function<std::unique_ptr<Instrument>(const InstrumentContext&)> create;

    const InstrumentParam* findParam(const QString& paramId) const;
};

namespace InstrumentRegistry {

bool add(const InstrumentDefinition& definition);
QVector<const InstrumentDefinition*> all(); // sorted by sortOrder, then name
const InstrumentDefinition* find(const QString& id);

// Creates the track's instrument; unknown types fall back to the first registered one
std::unique_ptr<Instrument> create(const InstrumentSettings& settings, int sampleRate);

} // namespace InstrumentRegistry

// Put in an instrument's .cpp at namespace scope:  REGISTER_INSTRUMENT(makeDefinition())
#define INSTRUMENT_CONCAT_INNER(a, b) a##b
#define INSTRUMENT_CONCAT(a, b) INSTRUMENT_CONCAT_INNER(a, b)
#define REGISTER_INSTRUMENT(definitionExpression) \
    namespace { const bool INSTRUMENT_CONCAT(instrumentRegistered_, __LINE__) = InstrumentRegistry::add(definitionExpression); }

#endif // INSTRUMENTAPI_H

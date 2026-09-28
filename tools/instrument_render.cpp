// Offline test for instruments: renders a fixed phrase through each registered
// instrument (in real-sized blocks, with a seek), checks the output and writes a WAV.
//
//   instrument_render [id|all] [outDir] [param=value ...]
//
// Exit code 0 = every check passed. Qt Core only; build with the command in
// src/audio/instruments/README.md, linking just the instruments you want to test.

#include "../src/audio/instruments/instrumentapi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cmath>
#include <cstdio>

namespace {

constexpr int SampleRate = 44100;
constexpr int Block = 512;

struct Result {
    bool ok = true;
    QStringList problems;
    float peak = 0.0f;
    double rms = 0.0;
};

std::vector<RenderNote> testPhrase()
{
    auto at = [](double seconds) { return qint64(seconds * SampleRate); };
    std::vector<RenderNote> notes;
    // Arpeggio across the range, varying velocity
    const int pitches[] = {36, 43, 48, 55, 60, 64, 67, 72, 79, 84};
    for (int i = 0; i < 10; ++i) {
        notes.push_back({at(0.25 * i), at(0.25 * i + 0.22), pitches[i], 0.4f + 0.06f * i});
    }
    // Chord, full velocity
    for (int p : {48, 60, 64, 67, 71}) {
        notes.push_back({at(2.75), at(3.75), p, 1.0f});
    }
    // Very short note, very quiet note, repeated/overlapping same pitch
    notes.push_back({at(4.0), at(4.005), 60, 1.0f});
    notes.push_back({at(4.25), at(4.5), 60, 0.05f});
    notes.push_back({at(4.6), at(5.2), 57, 0.9f});
    notes.push_back({at(4.8), at(5.0), 57, 0.9f});
    return notes;
}

void writeWav(const QString& path, const std::vector<float>& interleaved)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    const quint32 dataBytes = quint32(interleaved.size() * 2);
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF"); u32(36 + dataBytes); f.write("WAVE");
    f.write("fmt "); u32(16); u16(1); u16(2); u32(SampleRate); u32(SampleRate * 4); u16(4); u16(16);
    f.write("data"); u32(dataBytes);
    for (float s : interleaved) {
        const qint16 v = qint16(std::lround(qBound(-32768.0f, s, 32767.0f)));
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
}

Result run(const InstrumentDefinition& def, const InstrumentSettings& settings, const QString& outDir)
{
    Result r;
    std::unique_ptr<Instrument> instrument = def.create(InstrumentContext{settings, def, SampleRate});
    if (!instrument) {
        r.ok = false;
        r.problems << "factory returned null";
        return r;
    }

    const std::vector<RenderNote> notes = testPhrase();
    qint64 lastEnd = 0;
    for (const RenderNote& n : notes) {
        lastEnd = qMax(lastEnd, n.endFrame);
    }
    const qint64 tail = instrument->tailFrames();
    if (tail < 0 || tail > SampleRate * 30) {
        r.problems << QString("tailFrames() = %1 looks wrong").arg(tail);
        r.ok = false;
    }
    const qint64 total = lastEnd + qMax<qint64>(tail, 0) + SampleRate / 2;

    std::vector<float> out(size_t(total * 2), 0.0f);
    std::vector<float> block(Block * 2);
    // Pass 1: straight through. Pass 2: seek back to 1.0 s and render 2 s again (must not blow up).
    for (int pass = 0; pass < 2; ++pass) {
        const qint64 start = pass == 0 ? 0 : SampleRate;
        const qint64 end = pass == 0 ? total : SampleRate * 3;
        for (qint64 pos = start; pos < end; pos += Block) {
            const qint64 frames = qMin<qint64>(Block, end - pos);
            std::fill(block.begin(), block.end(), 0.0f);
            instrument->render(block.data(), pos, frames, notes);
            for (qint64 i = 0; i < frames * 2; ++i) {
                const float v = block[size_t(i)];
                if (!std::isfinite(v)) {
                    if (r.ok || !r.problems.contains("non-finite samples")) {
                        r.problems << "non-finite samples";
                    }
                    r.ok = false;
                    continue;
                }
                if (pass == 0) {
                    out[size_t(pos * 2 + i)] = v;
                }
            }
        }
    }

    double sum = 0;
    for (float v : out) {
        r.peak = qMax(r.peak, std::abs(v));
        sum += double(v) * v;
    }
    r.rms = std::sqrt(sum / qMax<size_t>(1, out.size()));
    if (r.peak < 100.0f) {
        r.ok = false;
        r.problems << "output is (almost) silent";
    }
    if (r.peak > 32767.0f) {
        r.ok = false;
        r.problems << QString("clips: peak %1 exceeds 16-bit range").arg(r.peak, 0, 'f', 0);
    }
    // Everything must have rung out once the last note's tail is over
    float tailPeak = 0.0f;
    for (qint64 f = lastEnd + qMax<qint64>(tail, 0) + SampleRate / 20; f < total; ++f) {
        tailPeak = qMax(tailPeak, qMax(std::abs(out[size_t(f * 2)]), std::abs(out[size_t(f * 2 + 1)])));
    }
    if (tailPeak > 30.0f) {
        r.ok = false;
        r.problems << QString("still sounding after tailFrames() (peak %1)").arg(tailPeak, 0, 'f', 0);
    }

    QDir().mkpath(outDir);
    writeWav(QDir(outDir).filePath(def.id + ".wav"), out);
    return r;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    const QString which = args.value(0, "all");
    const QString outDir = args.value(1, "instrument_renders");

    InstrumentSettings settings;
    for (int i = 2; i < args.size(); ++i) {
        const QStringList kv = args[i].split('=');
        if (kv.size() == 2) {
            settings.params.insert(kv[0], kv[1].toDouble());
        }
    }

    QTextStream out(stdout);
    int failures = 0;
    int tested = 0;
    for (const InstrumentDefinition* def : InstrumentRegistry::all()) {
        if (which != "all" && def->id != which) {
            continue;
        }
        ++tested;
        if (def->usesSample) {
            out << def->id << ": skipped (needs a sample)\n";
            continue;
        }
        // Every parameter must be declared sanely
        for (const InstrumentParam& p : def->params) {
            if (p.min > p.max || p.defaultValue < p.min || p.defaultValue > p.max) {
                out << def->id << ": parameter " << p.id << " has an invalid range/default\n";
                ++failures;
            }
        }
        settings.type = def->id;
        const Result r = run(*def, settings, outDir);
        const double peakDb = 20.0 * std::log10(qMax(1e-9, double(r.peak) / 32768.0));
        out << def->id << ": " << (r.ok ? "PASS" : "FAIL")
            << QString("  peak %1 dBFS, rms %2").arg(peakDb, 0, 'f', 1).arg(r.rms, 0, 'f', 0);
        if (!r.problems.isEmpty()) {
            out << "  [" << r.problems.join("; ") << "]";
        }
        out << "\n";
        failures += r.ok ? 0 : 1;
    }
    if (tested == 0) {
        out << "no instrument matched '" << which << "'\n";
        return 2;
    }
    out.flush();
    return failures == 0 ? 0 : 1;
}

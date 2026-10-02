#include "projectfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {

constexpr const char* FormatName = "bina-project";

QJsonObject pathToJson(const QString& path, const QDir& projectDir)
{
    return QJsonObject{
        {"path", projectDir.relativeFilePath(path)},
        {"absolutePath", QDir::cleanPath(QFileInfo(path).absoluteFilePath())},
    };
}

// The relative path wins (the project folder may have moved), then the absolute one
QString pathFromJson(const QJsonValue& value, const QDir& projectDir)
{
    const QJsonObject o = value.toObject();
    const QString relative = o.value("path").toString();
    const QString absolute = o.value("absolutePath").toString();
    if (!relative.isEmpty()) {
        const QString resolved = QDir::cleanPath(projectDir.absoluteFilePath(relative));
        if (QFileInfo::exists(resolved) || absolute.isEmpty()) {
            return resolved;
        }
    }
    return absolute;
}

QJsonObject trackToJson(const TrackData& t, const QDir& dir)
{
    QJsonObject params;
    for (auto it = t.instrument.params.cbegin(); it != t.instrument.params.cend(); ++it) {
        params.insert(it.key(), it.value());
    }
    QJsonObject instrument{{"type", t.instrument.type}, {"params", params}};
    if (!t.instrument.samplePath.isEmpty()) {
        instrument.insert("sample", pathToJson(t.instrument.samplePath, dir));
    }
    return QJsonObject{
        {"name", t.name},
        {"color", t.color.name()},
        {"muted", t.muted},
        {"soloed", t.soloed},
        {"volume", double(t.volume)},
        {"pan", double(t.pan)},
        {"effects", QJsonArray::fromStringList(t.effects)},
        {"instrument", instrument},
    };
}

TrackData trackFromJson(const QJsonObject& o, const QDir& dir)
{
    TrackData t;
    t.name = o.value("name").toString("Track");
    t.color = QColor(o.value("color").toString("#9a9aa4"));
    t.muted = o.value("muted").toBool();
    t.soloed = o.value("soloed").toBool();
    t.volume = float(qBound(0.0, o.value("volume").toDouble(1.0), 1.5));
    t.pan = float(qBound(-1.0, o.value("pan").toDouble(0.0), 1.0));
    for (const QJsonValue& e : o.value("effects").toArray()) {
        t.effects << e.toString();
    }
    const QJsonObject instrument = o.value("instrument").toObject();
    t.instrument.type = instrument.value("type").toString("synth");
    const QJsonObject params = instrument.value("params").toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        t.instrument.params.insert(it.key(), it.value().toDouble());
    }
    if (instrument.contains("sample")) {
        t.instrument.samplePath = pathFromJson(instrument.value("sample"), dir);
    }
    return t;
}

} // namespace

namespace ProjectFile {

QString fileDialogFilter()
{
    return "Bina Projects (*.bina);;All Files (*)";
}

bool save(const ProjectModel& model, const QString& path, QString* error)
{
    const QDir dir = QFileInfo(path).absoluteDir();

    QJsonArray tracks;
    for (int i = 0; i < model.trackCount(); ++i) {
        tracks.append(trackToJson(model.track(i), dir));
    }

    // Clips in timeline order, so the file reads naturally and diffs stay stable
    QList<const ClipData*> ordered;
    for (int id : model.clipIds()) {
        ordered << model.clip(id);
    }
    std::sort(ordered.begin(), ordered.end(), [](const ClipData* a, const ClipData* b) {
        return a->track != b->track ? a->track < b->track : a->start < b->start;
    });

    QJsonArray clips;
    for (const ClipData* c : ordered) {
        QJsonObject o{{"track", c->track}, {"start", c->start}};
        if (c->isNoteClip()) {
            QJsonArray notes;
            for (const Note& n : c->notes) {
                notes.append(QJsonObject{{"pitch", n.pitch}, {"start", n.start},
                                         {"length", n.length}, {"velocity", n.velocity}});
            }
            o.insert("kind", "notes");
            o.insert("lengthBeats", c->lengthBeats);
            o.insert("notes", notes);
        } else {
            o.insert("kind", "audio");
            o.insert("file", pathToJson(c->filePath, dir));
        }
        clips.append(o);
    }

    const QJsonObject root{
        {"format", FormatName},
        {"version", FormatVersion},
        {"tempo", model.tempo()},
        {"tracks", tracks},
        {"clips", clips},
    };

    // QSaveFile writes to a temporary and swaps it in, so a failed save never damages the old file
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool load(const QString& path, LoadedProject* project, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        *error = QString("Not a valid project file (%1)").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != FormatName) {
        *error = "Not a Bina project file";
        return false;
    }
    if (root.value("version").toInt() > FormatVersion) {
        *error = "This project was saved by a newer version of Bina";
        return false;
    }

    const QDir dir = QFileInfo(path).absoluteDir();
    project->tempo = root.value("tempo").toDouble(120.0);
    for (const QJsonValue& t : root.value("tracks").toArray()) {
        project->tracks.append(trackFromJson(t.toObject(), dir));
    }
    if (project->tracks.isEmpty()) {
        *error = "The project has no tracks";
        return false;
    }

    for (const QJsonValue& value : root.value("clips").toArray()) {
        const QJsonObject o = value.toObject();
        LoadedClip clip;
        clip.track = qBound(0, o.value("track").toInt(), int(project->tracks.size()) - 1);
        clip.start = qMax(0.0, o.value("start").toDouble());
        if (o.value("kind").toString() == "notes") {
            clip.kind = ClipData::Kind::Notes;
            clip.lengthBeats = qMax(1.0, o.value("lengthBeats").toDouble(ProjectModel::BeatsPerBar));
            for (const QJsonValue& n : o.value("notes").toArray()) {
                const QJsonObject no = n.toObject();
                Note note;
                note.pitch = qBound(0, no.value("pitch").toInt(60), 127);
                note.start = qMax(0.0, no.value("start").toDouble());
                note.length = qMax(1.0 / 64.0, no.value("length").toDouble(1.0));
                note.velocity = qBound(1, no.value("velocity").toInt(100), 127);
                clip.notes.append(note);
            }
        } else {
            clip.kind = ClipData::Kind::Audio;
            clip.filePath = pathFromJson(o.value("file"), dir);
        }
        project->clips.append(clip);
    }
    return true;
}

} // namespace ProjectFile

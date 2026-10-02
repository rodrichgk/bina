#ifndef PROJECTFILE_H
#define PROJECTFILE_H

#include "projectmodel.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Reads and writes .bina project files: readable JSON holding the tempo, every track
// (mix settings, effects, instrument) and every clip. Audio is referenced by path,
// never embedded; each path is stored relative to the project and absolute, so a
// project folder can be moved as a whole and still find its audio.
namespace ProjectFile {

constexpr int FormatVersion = 1;

QString fileDialogFilter();

// A clip as read from disk, before its audio is decoded
struct LoadedClip {
    ClipData::Kind kind = ClipData::Kind::Audio;
    int track = 0;
    double start = 0.0;
    QString filePath;      // audio clips: resolved path (may not exist)
    QVector<Note> notes;   // note clips
    double lengthBeats = 0.0;
};

struct LoadedProject {
    double tempo = 120.0;
    QVector<TrackData> tracks; // samplePath resolved; samples not decoded yet
    QVector<LoadedClip> clips;
};

bool save(const ProjectModel& model, const QString& path, QString* error);
bool load(const QString& path, LoadedProject* project, QString* error);

} // namespace ProjectFile

#endif // PROJECTFILE_H

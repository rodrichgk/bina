#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include <QColor>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

#include "audiobuffer.h"
#include "instrumentsettings.h"

// One note in a note clip. Times are in beats from the clip start, so notes follow the tempo.
struct Note {
    int pitch = 60;        // MIDI note number (60 = C4)
    double start = 0.0;    // beats
    double length = 1.0;   // beats
    int velocity = 100;    // 1..127

    double end() const { return start + length; }
    bool operator==(const Note& o) const
    {
        return pitch == o.pitch && start == o.start && length == o.length && velocity == o.velocity;
    }
};

// Everything that belongs to a track. A track holds audio clips and note clips side by
// side; its instrument plays the note clips. Clips inherit their look (color) and their
// sound processing (instrument, volume, pan, effects) from the track they sit on.
struct TrackData {
    QString name;
    QColor color;
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f; // 0..1.5
    float pan = 0.0f;    // -1 (left) .. 1 (right)
    QStringList effects; // insert chain, in processing order (see audio/effects.h)

    // Plays the track's note clips
    InstrumentSettings instrument;
    int instrumentRevision = 0; // unique per instrument change (project-wide), so the engine knows to rebuild it
};

struct ClipData {
    enum class Kind { Audio, Notes };

    int id = -1;
    Kind kind = Kind::Audio;
    int track = 0;
    double start = 0.0; // seconds

    // Audio clips
    QString filePath;
    std::shared_ptr<const AudioBuffer> audio;
    QVector<qreal> peaks; // normalized 0..1, for drawing

    // Note clips
    QVector<Note> notes;
    double lengthBeats = 0.0;

    bool isNoteClip() const { return kind == Kind::Notes; }
};

// Single source of truth for tracks, clips and tempo.
// The timeline and piano roll draw it, the audio engine renders it; none of them own project state.
class ProjectModel : public QObject
{
    Q_OBJECT

public:
    static constexpr int BeatsPerBar = 4;

    explicit ProjectModel(QObject* parent = nullptr);

    // Replaces the whole project (new or opened file): every view rebuilds on projectReset()
    void resetProject(const QVector<TrackData>& tracks, double tempo);

    // Tempo
    double tempo() const { return m_tempo; }
    void setTempo(double bpm);
    double secondsPerBeat() const { return 60.0 / m_tempo; }

    // Tracks
    int trackCount() const { return m_tracks.size(); }
    const TrackData& track(int index) const { return m_tracks.at(index); }
    bool isValidTrack(int index) const { return index >= 0 && index < m_tracks.size(); }
    int addTrack(const QString& name, const QColor& color);
    void setTrackName(int index, const QString& name);
    void setTrackMuted(int index, bool muted);
    void setTrackSoloed(int index, bool soloed);
    void setTrackVolume(int index, float volume);
    void setTrackPan(int index, float pan);
    void setTrackColor(int index, const QColor& color);
    void setTrackEffects(int index, const QStringList& effects);
    void setTrackInstrument(int index, const InstrumentSettings& instrument);
    bool anyTrackSoloed() const;
    // False when muted, or when another track is soloed
    bool isTrackAudible(int index) const;

    bool trackHasNotes(int track) const;

    // Clips
    QList<int> clipIds() const { return m_clips.keys(); }
    const ClipData* clip(int id) const;
    int addClip(ClipData clip); // assigns and returns the id, or -1 for an invalid track
    int addNoteClip(int track, double startSeconds, double lengthBeats);
    void moveClip(int id, int track, double start);
    void setClipNotes(int id, const QVector<Note>& notes); // grows the clip to fit its notes
    void removeClip(int id);
    void clearClips();
    bool hasClips() const { return !m_clips.isEmpty(); }

    // Clip timing in seconds (note clips depend on the tempo)
    double clipDuration(const ClipData& clip) const;
    double clipEnd(const ClipData& clip) const { return clip.start + clipDuration(clip); }

    double trackEnd(int track) const; // end of the last clip on a track
    double length() const { return m_length; }

signals:
    void projectReset();
    void tempoChanged(double bpm);
    void trackAdded(int index);
    void trackChanged(int index);
    void clipAdded(int id);
    void clipChanged(int id);
    void clipRemoved(int id);
    void lengthChanged(double seconds);

private:
    void updateLength();

    QVector<TrackData> m_tracks;
    QMap<int, ClipData> m_clips;
    int m_nextClipId = 1;
    int m_nextRevision = 1;
    double m_tempo = 120.0;
    double m_length = 0.0;
};

#endif // PROJECTMODEL_H

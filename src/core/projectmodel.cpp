#include "projectmodel.h"

#include <QtMath>

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Whole project

void ProjectModel::resetProject(const QVector<TrackData>& tracks, double tempo)
{
    m_clips.clear();
    m_tracks = tracks;
    for (TrackData& t : m_tracks) {
        t.instrumentRevision = m_nextRevision++;
    }
    m_tempo = qBound(20.0, tempo, 400.0);
    m_length = 0.0;
    emit projectReset();
    emit tempoChanged(m_tempo);
    emit lengthChanged(m_length);
}

// ---------------------------------------------------------------------------
// Tempo

void ProjectModel::setTempo(double bpm)
{
    bpm = qBound(20.0, bpm, 400.0);
    if (qFuzzyCompare(bpm, m_tempo)) {
        return;
    }
    m_tempo = bpm;
    emit tempoChanged(m_tempo);
    // Note clips are measured in beats, so their length on the timeline changes
    for (const ClipData& c : std::as_const(m_clips)) {
        if (c.isNoteClip()) {
            emit clipChanged(c.id);
        }
    }
    updateLength();
}

// ---------------------------------------------------------------------------
// Tracks

int ProjectModel::addTrack(const QString& name, const QColor& color)
{
    TrackData track;
    track.name = name;
    track.color = color;
    track.instrumentRevision = m_nextRevision++;
    m_tracks.append(track);
    const int index = m_tracks.size() - 1;
    emit trackAdded(index);
    return index;
}

void ProjectModel::setTrackName(int index, const QString& name)
{
    if (!isValidTrack(index) || m_tracks[index].name == name) {
        return;
    }
    m_tracks[index].name = name;
    emit trackChanged(index);
}

void ProjectModel::setTrackMuted(int index, bool muted)
{
    if (!isValidTrack(index) || m_tracks[index].muted == muted) {
        return;
    }
    m_tracks[index].muted = muted;
    emit trackChanged(index);
}

void ProjectModel::setTrackSoloed(int index, bool soloed)
{
    if (!isValidTrack(index) || m_tracks[index].soloed == soloed) {
        return;
    }
    m_tracks[index].soloed = soloed;
    emit trackChanged(index);
}

void ProjectModel::setTrackVolume(int index, float volume)
{
    volume = qBound(0.0f, volume, 1.5f);
    if (!isValidTrack(index) || qFuzzyCompare(m_tracks[index].volume, volume)) {
        return;
    }
    m_tracks[index].volume = volume;
    emit trackChanged(index);
}

void ProjectModel::setTrackPan(int index, float pan)
{
    pan = qBound(-1.0f, pan, 1.0f);
    if (!isValidTrack(index) || qFuzzyCompare(m_tracks[index].pan + 2.0f, pan + 2.0f)) {
        return;
    }
    m_tracks[index].pan = pan;
    emit trackChanged(index);
}

void ProjectModel::setTrackColor(int index, const QColor& color)
{
    if (!isValidTrack(index) || m_tracks[index].color == color) {
        return;
    }
    m_tracks[index].color = color;
    emit trackChanged(index);
}

void ProjectModel::setTrackEffects(int index, const QStringList& effects)
{
    if (!isValidTrack(index) || m_tracks[index].effects == effects) {
        return;
    }
    m_tracks[index].effects = effects;
    emit trackChanged(index);
}

void ProjectModel::setTrackInstrument(int index, const InstrumentSettings& instrument)
{
    if (!isValidTrack(index)) {
        return;
    }
    m_tracks[index].instrument = instrument;
    m_tracks[index].instrumentRevision = m_nextRevision++;
    emit trackChanged(index);
}

bool ProjectModel::anyTrackSoloed() const
{
    for (const TrackData& t : m_tracks) {
        if (t.soloed) {
            return true;
        }
    }
    return false;
}

bool ProjectModel::isTrackAudible(int index) const
{
    if (!isValidTrack(index)) {
        return false;
    }
    const TrackData& t = m_tracks[index];
    if (t.muted) {
        return false;
    }
    return !anyTrackSoloed() || t.soloed;
}

// ---------------------------------------------------------------------------
// Clips

bool ProjectModel::trackHasNotes(int track) const
{
    for (const ClipData& c : m_clips) {
        if (c.track == track && c.isNoteClip()) {
            return true;
        }
    }
    return false;
}

const ClipData* ProjectModel::clip(int id) const
{
    auto it = m_clips.constFind(id);
    return it == m_clips.constEnd() ? nullptr : &it.value();
}

int ProjectModel::addClip(ClipData clip)
{
    if (!isValidTrack(clip.track)) {
        return -1;
    }
    clip.id = m_nextClipId++;
    clip.start = qMax(0.0, clip.start);
    m_clips.insert(clip.id, clip);
    emit clipAdded(clip.id);
    updateLength();
    return clip.id;
}

int ProjectModel::addNoteClip(int track, double startSeconds, double lengthBeats)
{
    ClipData clip;
    clip.kind = ClipData::Kind::Notes;
    clip.track = track;
    clip.start = startSeconds;
    clip.lengthBeats = qMax(1.0, lengthBeats);
    return addClip(clip);
}

void ProjectModel::moveClip(int id, int track, double start)
{
    auto it = m_clips.find(id);
    if (it == m_clips.end()) {
        return;
    }
    if (!isValidTrack(track)) {
        return;
    }
    start = qMax(0.0, start);
    if (it->track == track && qFuzzyCompare(it->start + 1.0, start + 1.0)) {
        return;
    }
    it->track = track;
    it->start = start;
    emit clipChanged(id);
    updateLength();
}

void ProjectModel::setClipNotes(int id, const QVector<Note>& notes)
{
    auto it = m_clips.find(id);
    if (it == m_clips.end() || !it->isNoteClip() || it->notes == notes) {
        return;
    }
    it->notes = notes;
    // Grow to the end of the last note, rounded up to a whole bar
    double lastEnd = 0.0;
    for (const Note& n : notes) {
        lastEnd = qMax(lastEnd, n.end());
    }
    const double bars = std::ceil(lastEnd / BeatsPerBar - 1e-9);
    it->lengthBeats = qMax(it->lengthBeats, bars * BeatsPerBar);
    emit clipChanged(id);
    updateLength();
}

void ProjectModel::removeClip(int id)
{
    if (m_clips.remove(id) == 0) {
        return;
    }
    emit clipRemoved(id);
    updateLength();
}

void ProjectModel::clearClips()
{
    const QList<int> ids = m_clips.keys();
    for (int id : ids) {
        removeClip(id);
    }
}

double ProjectModel::clipDuration(const ClipData& clip) const
{
    if (clip.isNoteClip()) {
        return clip.lengthBeats * secondsPerBeat();
    }
    return clip.audio ? clip.audio->duration() : 0.0;
}

double ProjectModel::trackEnd(int track) const
{
    double end = 0.0;
    for (const ClipData& c : m_clips) {
        if (c.track == track) {
            end = qMax(end, clipEnd(c));
        }
    }
    return end;
}

void ProjectModel::updateLength()
{
    double length = 0.0;
    for (const ClipData& c : m_clips) {
        length = qMax(length, clipEnd(c));
    }
    if (!qFuzzyCompare(length + 1.0, m_length + 1.0)) {
        m_length = length;
        emit lengthChanged(m_length);
    }
}

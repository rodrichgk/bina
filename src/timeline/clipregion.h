#ifndef CLIPREGION_H
#define CLIPREGION_H

#include "../core/projectmodel.h"

#include <QColor>
#include <QGraphicsRectItem>
#include <QObject>
#include <QVector>
#include <functional>

class TimeScale;

// View of one clip on the timeline: a waveform for audio clips, a miniature
// piano roll for note clips. Geometry is local (0,0,width,height); the timeline
// places it with setPos() from the model. Drags, removal and editing are requests.
class ClipRegion : public QObject, public QGraphicsRectItem
{
    Q_OBJECT

public:
    ClipRegion(int clipId, const QColor& color, int laneHeight, const TimeScale* scale);

    int clipId() const { return m_clipId; }

    void setDuration(double seconds); // Also call after the zoom changes

    // Rounds a dragged start time to the grid (Alt while dragging bypasses it)
    void setSnap(std::function<double(double)> snap) { m_snap = std::move(snap); }
    void setWaveform(const QVector<qreal>& peaks);
    void setNotes(const QVector<Note>& notes, double lengthBeats);

    // Lane layout used to snap drags onto tracks
    void setLaneLayout(qreal firstLaneY, qreal laneHeight, int laneCount);

    // Inaudible clips (muted or not soloed) are drawn dimmed
    void setAudible(bool audible);

    // Always the owning track's color
    void setColor(const QColor& color);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void moveRequested(int clipId, int track, double startSeconds);
    void removeRequested(int clipId);
    void editRequested(int clipId);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    int laneAt(qreal y) const;
    void paintWaveform(QPainter* painter, const QRectF& area) const;
    void paintNotes(QPainter* painter, const QRectF& area) const;

    int m_clipId;
    const TimeScale* m_scale;
    std::function<double(double)> m_snap;
    QColor m_color;
    bool m_audible = true;

    bool m_isNoteClip = false;
    QVector<qreal> m_peaks;
    QVector<Note> m_notes;
    double m_lengthBeats = 0.0;

    qreal m_firstLaneY = 0;
    qreal m_laneHeight = 0;
    int m_laneCount = 0;
};

#endif // CLIPREGION_H

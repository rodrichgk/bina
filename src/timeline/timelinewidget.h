#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QGraphicsScene>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QSplitter>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "../core/projectmodel.h"
#include "../core/timescale.h"
#include "TimelineIndicator.h"
#include "clipregion.h"
#include "trackheaderwidget.h"
#include "tracklane.h"

class QGraphicsOpacityEffect;
class QMimeData;
class TimelineRuler;
class TimelineView;

// View of the ProjectModel: one header + lane per track, one ClipRegion per clip,
// on a musical (bars and beats) timeline that zooms with Ctrl+wheel.
// User edits (drag, remove, mute) go to the model; the view updates from its signals.
class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(ProjectModel* model, QWidget* parent = nullptr);

    // Playhead
    void setIndicatorPosition(double seconds);
    double indicatorPosition() const;

    void setPlaybackMode(bool isPlaying);

signals:
    void indicatorPositionChanged(double seconds);
    // Audio files dropped on the timeline: first file goes on `track` at `seconds`, the rest below it
    void filesDropped(const QStringList& filePaths, int track, double seconds);
    // A note clip should open in the piano roll
    void clipEditRequested(int clipId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // Model -> view
    void onTrackAdded(int index);
    void onTrackChanged(int index);
    void onClipAdded(int id);
    void onClipChanged(int id);
    void onClipRemoved(int id);

    // View -> model
    void onRegionMoveRequested(int clipId, int track, double startSeconds);
    void onRegionRemoveRequested(int clipId);
    void openTrackSettingsDialog(int trackIndex);

    void onIndicatorMoved(TimelineIndicator* indicator);
    void onTimelineScrolled(int value);
    void performScroll();
    void zoom(double factor, int viewportX);

private:
    void setupUi();
    void setupTrackList();
    void setupView();
    void setupIndicator();

    qreal laneY(int track) const { return track * m_trackHeight; }
    int laneAt(qreal sceneY) const;
    double snap(double seconds) const; // To the grid visible at the current zoom
    void placeRegion(ClipRegion* region, const ClipData& clip);
    void refreshRegionContent(ClipRegion* region, const ClipData& clip);
    void createNoteClipAt(const QPointF& scenePos);
    void refreshHeader(int index);
    void refreshAllHeaders();
    void refreshRegionLooks();
    void relayout();   // After zoom or tempo changes: every x position is recomputed
    void updateSceneSize();
    void updateEmptyState();
    void focusOnItem(QGraphicsItem* item);

    // Drag & drop of audio files
    QStringList droppableFiles(const QMimeData* mime) const;
    void showDropTarget(const QPointF& scenePos, int fileCount);

    ProjectModel* m_model;
    TimeScale m_scale;

    QSplitter* m_splitter = nullptr;
    QListWidget* m_trackList = nullptr;
    QGraphicsScene* m_scene = nullptr;
    TimelineView* m_view = nullptr;
    TimelineRuler* m_ruler = nullptr;
    QLabel* m_emptyHint = nullptr;
    QGraphicsOpacityEffect* m_emptyHintOpacity = nullptr;
    QPointer<QVariantAnimation> m_pageAnimation;
    TimelineIndicator* m_indicator = nullptr;
    QGraphicsRectItem* m_dropHighlight = nullptr;
    int m_dragFileCount = 0;

    QList<TrackHeaderWidget*> m_headers;
    QList<TrackLane*> m_lanes;
    QHash<int, ClipRegion*> m_regions; // by clip id

    int m_trackHeight;
    int m_trackIdWidth;

    QTimer* m_scrollTimer = nullptr;
    bool m_scrollLeft = false;
    bool m_scrollRight = false;
    bool m_isPlaybackMode = false;

    static constexpr int MinimumBars = 64; // The timeline is at least this long
    static constexpr int TrailingBars = 8; // Room after the last clip to drag into
};

#endif // TIMELINEWIDGET_H

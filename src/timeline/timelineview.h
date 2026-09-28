#ifndef TIMELINEVIEW_H
#define TIMELINEVIEW_H

#include <QGraphicsView>

class ProjectModel;
class TimeScale;

// The timeline's graphics view. Draws the musical grid behind the clips (so it's
// crisp at every zoom and needs no scene items), and turns Ctrl+wheel into zoom
// and Shift+wheel into horizontal scrolling.
class TimelineView : public QGraphicsView
{
    Q_OBJECT

public:
    TimelineView(QGraphicsScene* scene, const ProjectModel* model, const TimeScale* scale, QWidget* parent = nullptr);

signals:
    // factor > 1 zooms in, anchored at viewport x
    void zoomRequested(double factor, int viewportX);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    const ProjectModel* m_model;
    const TimeScale* m_scale;
};

#endif // TIMELINEVIEW_H

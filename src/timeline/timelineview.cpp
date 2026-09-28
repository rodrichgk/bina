#include "timelineview.h"
#include "musicalgrid.h"
#include "../core/timescale.h"
#include "../theme.h"

#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

TimelineView::TimelineView(QGraphicsScene* scene, const ProjectModel* model, const TimeScale* scale, QWidget* parent)
    : QGraphicsView(scene, parent)
    , m_model(model)
    , m_scale(scale)
{
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

void TimelineView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, Theme::Base);

    const double spb = m_model->secondsPerBeat();
    const double pxPerBeat = m_scale->toX(spb);
    const double step = MusicalGrid::stepBeats(pxPerBeat);

    // Bars strongest, beats next, subdivisions faintest; lines at every visible grid step
    const double firstBeat = std::floor(m_scale->toSeconds(rect.left()) / spb / step) * step;
    const double lastBeat = m_scale->toSeconds(rect.right()) / spb;
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (double beat = firstBeat; beat <= lastBeat + step; beat += step) {
        const double x = std::round(m_scale->toX(beat * spb)) + 0.5;
        const bool bar = std::fmod(beat, ProjectModel::BeatsPerBar) < 1e-6;
        const bool wholeBeat = std::fmod(beat, 1.0) < 1e-6;
        painter->setPen(bar ? Theme::Border.lighter(120) : wholeBeat ? Theme::GridMajor : Theme::GridMinor);
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    }
}

void TimelineView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(std::pow(1.0015, delta), int(event->position().x()));
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        QScrollBar* bar = horizontalScrollBar();
        bar->setValue(bar->value() - delta);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

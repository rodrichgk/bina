#include "clipregion.h"
#include "../core/timescale.h"

#include <QApplication>
#include "../theme.h"

#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

ClipRegion::ClipRegion(int clipId, const QColor& color, int laneHeight, const TimeScale* scale)
    : m_clipId(clipId)
    , m_scale(scale)
    , m_color(color)
{
    setRect(0, 0, 1, laneHeight);
    setZValue(1);
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
}

void ClipRegion::setDuration(double seconds)
{
    const qreal width = qMax(1.0, m_scale->toX(seconds));
    if (!qFuzzyCompare(rect().width(), width)) {
        prepareGeometryChange();
        setRect(0, 0, width, rect().height());
    }
}

void ClipRegion::setWaveform(const QVector<qreal>& peaks)
{
    m_isNoteClip = false;
    m_peaks = peaks;
    update();
}

void ClipRegion::setNotes(const QVector<Note>& notes, double lengthBeats)
{
    m_isNoteClip = true;
    m_notes = notes;
    m_lengthBeats = lengthBeats;
    setToolTip("Double-click to edit notes");
    update();
}

void ClipRegion::setLaneLayout(qreal firstLaneY, qreal laneHeight, int laneCount)
{
    m_firstLaneY = firstLaneY;
    m_laneHeight = laneHeight;
    m_laneCount = laneCount;
}

void ClipRegion::setAudible(bool audible)
{
    if (m_audible != audible) {
        m_audible = audible;
        update();
    }
}

void ClipRegion::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        update();
    }
}

int ClipRegion::laneAt(qreal y) const
{
    if (m_laneHeight <= 0 || m_laneCount <= 0) {
        return 0;
    }
    const int lane = qRound((y - m_firstLaneY) / m_laneHeight);
    return qBound(0, lane, m_laneCount - 1);
}

QVariant ClipRegion::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        // Keep clips on the timeline, on a lane, and on the grid while dragging
        const QPointF p = value.toPointF();
        double seconds = qMax(0.0, m_scale->toSeconds(p.x()));
        if (m_snap && !(QApplication::keyboardModifiers() & Qt::AltModifier)) {
            seconds = qMax(0.0, m_snap(seconds));
        }
        return QPointF(m_scale->toX(seconds), m_firstLaneY + laneAt(p.y()) * m_laneHeight);
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void ClipRegion::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::ClosedHandCursor);
    }
    QGraphicsRectItem::mousePressEvent(event);
}

void ClipRegion::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsRectItem::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
    // The model decides the final position; the timeline re-places the item from it
    emit moveRequested(m_clipId, laneAt(pos().y()), m_scale->toSeconds(pos().x()));
}

void ClipRegion::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_isNoteClip && event->button() == Qt::LeftButton) {
        emit editRequested(m_clipId);
        event->accept();
        return;
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

void ClipRegion::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu menu;
    QAction* editAction = m_isNoteClip ? menu.addAction("Edit Notes...") : nullptr;
    QAction* removeAction = menu.addAction("Remove Clip");
    QAction* chosen = menu.exec(event->screenPos());
    if (chosen && chosen == editAction) {
        emit editRequested(m_clipId);
    } else if (chosen == removeAction) {
        emit removeRequested(m_clipId); // Delivered queued: this item may be deleted by it
    }
    event->accept();
}

void ClipRegion::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    if (!m_audible) {
        painter->setOpacity(0.35);
    }

    // Inset so clips on adjacent tracks never touch
    const QRectF body = rect().adjusted(0.5, 2.5, -0.5, -2.5);
    const qreal radius = 4;
    const bool selected = isSelected();

    // Tinted body: clip color mixed into the canvas, full color reserved for content + edge
    const QColor fill = Theme::mix(m_color, Theme::Base, selected ? 0.42 : 0.32);
    const QColor edge = selected ? Theme::Accent : Theme::mix(m_color, Theme::Base, 0.75);

    painter->setPen(QPen(edge, selected ? 1.5 : 1.0));
    painter->setBrush(fill);
    painter->drawRoundedRect(body, radius, radius);

    QPainterPath clipPath;
    clipPath.addRoundedRect(body, radius, radius);
    painter->setClipPath(clipPath);

    // Top color bar identifies the clip at a glance
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color);
    painter->drawRect(QRectF(body.left(), body.top(), body.width(), 3));

    const QRectF content = body.adjusted(0, 5, 0, -2);
    if (m_isNoteClip) {
        paintNotes(painter, content);
    } else {
        paintWaveform(painter, content);
    }

    painter->setClipping(false);
}

void ClipRegion::paintWaveform(QPainter* painter, const QRectF& area) const
{
    // One vertical line per ~1.5px, each showing the loudest peak it covers
    if (m_peaks.isEmpty() || area.width() <= 1) {
        return;
    }
    const qreal centerY = area.center().y();
    const qreal halfHeight = area.height() * 0.44;
    const int lines = qBound(1, int(area.width() / 1.5), 4000);
    const qreal step = area.width() / lines;
    const qreal peaksPerLine = qreal(m_peaks.size()) / lines;

    QPainterPath wavePath;
    for (int i = 0; i < lines; ++i) {
        const int first = int(i * peaksPerLine);
        const int last = qMin(int(m_peaks.size()), qMax(first + 1, int((i + 1) * peaksPerLine)));
        qreal peak = 0;
        for (int p = first; p < last; ++p) {
            peak = qMax(peak, m_peaks[p]);
        }
        const qreal x = area.left() + i * step;
        wavePath.moveTo(x, centerY - peak * halfHeight);
        wavePath.lineTo(x, centerY + peak * halfHeight);
    }
    painter->setPen(QPen(m_color.lighter(125), 1.0, Qt::SolidLine, Qt::FlatCap));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(wavePath);
}

void ClipRegion::paintNotes(QPainter* painter, const QRectF& area) const
{
    // Faint bar lines so the clip reads as musical time
    const qreal pxPerBeat = m_lengthBeats > 0 ? area.width() / m_lengthBeats : 0;
    if (pxPerBeat > 0) {
        painter->setPen(QPen(Theme::mix(m_color, Theme::Base, 0.45), 1));
        for (int bar = ProjectModel::BeatsPerBar; bar < m_lengthBeats; bar += ProjectModel::BeatsPerBar) {
            const qreal x = area.left() + bar * pxPerBeat;
            painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
    }

    if (m_notes.isEmpty()) {
        if (area.width() > 150) {
            painter->setPen(Theme::mix(m_color, Theme::Text, 0.35));
            painter->setFont(Theme::uiFont(8));
            painter->drawText(area.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              "Empty. Double-click to add notes");
        }
        return;
    }

    // Fit the used pitch range into the clip height (at least an octave, so single notes don't fill it)
    int low = 127, high = 0;
    for (const Note& n : m_notes) {
        low = qMin(low, n.pitch);
        high = qMax(high, n.pitch);
    }
    const int span = qMax(12, high - low + 1);
    const int bottom = low - (span - (high - low + 1)) / 2;
    const qreal rowHeight = area.height() / span;
    const qreal barHeight = qMax(1.5, rowHeight - 1);

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color.lighter(130));
    for (const Note& n : m_notes) {
        if (n.start >= m_lengthBeats) {
            continue;
        }
        const qreal x = area.left() + n.start * pxPerBeat;
        const qreal w = qMax(1.5, qMin(n.length, m_lengthBeats - n.start) * pxPerBeat - 1);
        const qreal y = area.bottom() - (n.pitch - bottom + 1) * rowHeight;
        painter->drawRoundedRect(QRectF(x, y, w, barHeight), 1, 1);
    }
}

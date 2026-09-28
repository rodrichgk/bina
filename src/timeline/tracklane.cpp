#include "tracklane.h"
#include "../theme.h"

#include <QPainter>

TrackLane::TrackLane(int height, qreal width)
    : m_height(height)
    , m_width(width)
{
}

void TrackLane::setWidth(qreal width)
{
    prepareGeometryChange();
    m_width = width;
}

QRectF TrackLane::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

void TrackLane::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Separator at the bottom of the lane
    const QRectF rect = boundingRect();
    painter->setPen(QPen(Theme::Divider, 1));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());
}

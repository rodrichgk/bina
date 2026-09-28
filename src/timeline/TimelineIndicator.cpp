#include "TimelineIndicator.h"
#include <QDebug>
#include <QTime>
#include <QStyleOptionGraphicsItem>
#include "../theme.h"

TimelineIndicator::TimelineIndicator(qreal height, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_height(height) {

    setZValue(1);
    setFlags(ItemIsMovable | ItemSendsGeometryChanges|ItemIsSelectable);
}

QRectF TimelineIndicator::boundingRect() const {
    return QRectF(-10, 0, 20, m_height); // Adjust the width as needed
}

void TimelineIndicator::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Optimize rendering based on level of detail
    if (m_optimizedRendering) {
        // Only enable antialiasing for high-quality rendering
        const QStyleOptionGraphicsItem* opt = qstyleoption_cast<const QStyleOptionGraphicsItem*>(option);
        if (opt && opt->levelOfDetailFromTransform(painter->worldTransform()) < 0.5) {
            // Skip detailed rendering at low zoom levels
            painter->setRenderHint(QPainter::Antialiasing, false);
        } else {
            painter->setRenderHint(QPainter::Antialiasing, true);
        }
    }
    
    // Use efficient drawing without unnecessary fills
    // Don't clear the entire bounding rect - let the scene handle background
    
    // Playhead: compact amber head + hairline
    static const QPolygonF head({
        QPointF(-6, 0), QPointF(6, 0), QPointF(6, 6), QPointF(0, 12), QPointF(-6, 6)
    });
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::Accent);
    painter->drawPolygon(head);
    
    painter->setPen(QPen(Theme::Accent, 1.25));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(0, 12), QPointF(0, m_height));
}

void TimelineIndicator::mouseMoveEvent(QGraphicsSceneMouseEvent *event){
    QGraphicsItem::mouseMoveEvent(event);
    
    // Always emit the signal to ensure transport dock stays in sync
    emit indicatorMoved(this);
}

void TimelineIndicator::mouseReleaseEvent(QGraphicsSceneMouseEvent *event){
    QGraphicsItem::mouseReleaseEvent(event);
}

void TimelineIndicator::mousePressEvent(QGraphicsSceneMouseEvent *event){

    QGraphicsItem::mousePressEvent(event);
}

QVariant TimelineIndicator::itemChange(GraphicsItemChange change, const QVariant &value){
    if(change == ItemPositionChange){
        QPointF newPos = value.toPointF();
        // Constrain to valid X position and maintain Y position
        if(newPos.x() < 0) {
            newPos.setX(0);
        }
        newPos.setY(pos().y()); // Keep Y position fixed
        
        // Throttle position change notifications
        if (m_throttleUpdates && change == ItemPositionChange) {
            QTime currentTime = QTime::currentTime();
            if (m_lastUpdateTime.isValid() && 
                m_lastUpdateTime.msecsTo(currentTime) < UPDATE_THROTTLE_MS) {
                // Still apply the position change, just don't emit signals
                return newPos;
            }
            m_lastUpdateTime = currentTime;
        }
        
        return newPos;
    }
    return QGraphicsItem::itemChange(change,value);
}

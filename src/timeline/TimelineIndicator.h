#ifndef TIMELINEINDICATOR_H
#define TIMELINEINDICATOR_H

#include <QGraphicsItem>
#include <QPainter>
#include <QTime>

class TimelineIndicator :public QObject, public QGraphicsItem {
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    explicit TimelineIndicator(qreal height, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void setHeight(qreal height) { prepareGeometryChange(); m_height = height; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    
    // Performance optimization methods
    void setOptimizedRendering(bool enabled) { m_optimizedRendering = enabled; }
    void throttleUpdates(bool enabled) { m_throttleUpdates = enabled; }

private:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    
    qreal m_height;
    bool m_optimizedRendering = true;
    bool m_throttleUpdates = true;
    QTime m_lastUpdateTime;
    static const int UPDATE_THROTTLE_MS = 16; // 60fps limit

signals:
    void indicatorMoved(TimelineIndicator* indicator);
};

#endif // TIMELINEINDICATOR_H

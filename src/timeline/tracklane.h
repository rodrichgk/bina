#ifndef TRACKLANE_H
#define TRACKLANE_H

#include <QGraphicsItem>

// The background row of one track on the timeline. Purely visual:
// track state (name, mute, volume...) lives in ProjectModel.
class TrackLane : public QGraphicsItem
{
public:
    TrackLane(int height, qreal width);

    void setWidth(qreal width);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
    int m_height;
    qreal m_width;
};

#endif // TRACKLANE_H

#ifndef MOTION_H
#define MOTION_H

#include <QGraphicsEffect>
#include <QObject>
#include <QPointer>
#include <QVariantAnimation>

class QApplication;

// Draws its widget scaled around the center and/or faded. Qt widgets can't be
// scaled directly, so press feedback renders the button through this effect.
class PressEffect : public QGraphicsEffect
{
    Q_OBJECT

public:
    explicit PressEffect(QObject* parent = nullptr);

    qreal scale() const { return m_scale; }
    void setScale(qreal scale);
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    // Kept attached when idle (e.g. the record button also uses it to pulse)
    void setPersistent(bool persistent) { m_persistent = persistent; }
    bool isPersistent() const { return m_persistent; }

    void pressDown(); // sinks in
    void release();   // springs back with a little overshoot

protected:
    QRectF boundingRectFor(const QRectF& rect) const override;
    void draw(QPainter* painter) override;

private:
    qreal m_scale = 1.0;
    qreal m_opacity = 1.0;
    bool m_persistent = false;
    QPointer<QVariantAnimation> m_animation;
};

namespace Motion {

// App-wide: every push/tool button reacts to presses, dialogs rise in, popups fade in.
void install(QApplication& app);

// The widget's PressEffect, created (persistent) if needed
PressEffect* pressEffect(QWidget* widget);

} // namespace Motion

#endif // MOTION_H

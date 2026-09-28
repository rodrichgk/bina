#include "motion.h"
#include "../theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

namespace {

constexpr qreal PressedScale = 0.90;
constexpr int PressMs = 90;
constexpr int ReleaseMs = 320;
constexpr qreal ReleaseOvershoot = 2.6; // OutBack overshoot: a visible but small bounce
constexpr int DialogMs = 240;
constexpr int DialogRise = 14;          // px the dialog travels up while fading in
constexpr int PopupMs = 130;
constexpr int PopupSlide = 6;

} // namespace

// ===========================================================================
// PressEffect

PressEffect::PressEffect(QObject* parent)
    : QGraphicsEffect(parent)
{
}

void PressEffect::setScale(qreal scale)
{
    if (!qFuzzyCompare(scale, m_scale)) {
        m_scale = scale;
        update();
    }
}

void PressEffect::setOpacity(qreal opacity)
{
    if (!qFuzzyCompare(opacity, m_opacity)) {
        m_opacity = opacity;
        update();
    }
}

QRectF PressEffect::boundingRectFor(const QRectF& rect) const
{
    // Room for the overshoot to draw past the widget's edges
    const qreal dx = rect.width() * 0.08;
    const qreal dy = rect.height() * 0.08;
    return rect.adjusted(-dx, -dy, dx, dy);
}

void PressEffect::draw(QPainter* painter)
{
    if (qFuzzyCompare(m_scale, 1.0) && qFuzzyCompare(m_opacity, 1.0)) {
        drawSource(painter);
        return;
    }
    QPoint offset;
    const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::NoPad);
    const QPointF center = sourceBoundingRect(Qt::LogicalCoordinates).center();

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setOpacity(painter->opacity() * m_opacity);
    painter->translate(center);
    painter->scale(m_scale, m_scale);
    painter->translate(-center);
    painter->drawPixmap(offset, pixmap);
    painter->restore();
}

void PressEffect::pressDown()
{
    if (m_animation) {
        m_animation->stop();
    }
    m_animation = Theme::animate(this, m_scale, PressedScale, PressMs,
                                 [this](qreal v) { setScale(v); });
}

void PressEffect::release()
{
    if (m_animation) {
        m_animation->stop();
    }
    QEasingCurve spring(QEasingCurve::OutBack);
    spring.setOvershoot(ReleaseOvershoot);
    m_animation = Theme::animate(this, m_scale, 1.0, ReleaseMs, [this](qreal v) { setScale(v); },
        [this]() {
            // Idle effects cost an offscreen render per paint; detach once settled
            if (m_persistent || !qFuzzyCompare(m_opacity, 1.0)) {
                return;
            }
            QPointer<QWidget> widget = qobject_cast<QWidget*>(parent());
            QPointer<PressEffect> self(this);
            QTimer::singleShot(0, widget, [widget, self]() {
                if (widget && self && widget->graphicsEffect() == self && qFuzzyCompare(self->scale(), 1.0)) {
                    widget->setGraphicsEffect(nullptr); // Deletes the effect
                }
            });
        }, spring);
}

// ===========================================================================
// App-wide filter

namespace {

bool wantsPressFeedback(QObject* object)
{
    auto* button = qobject_cast<QAbstractButton*>(object);
    if (!button || !button->isEnabled()) {
        return false;
    }
    // Push and tool buttons bounce; checkboxes/radios keep their own indicator
    if (!qobject_cast<QPushButton*>(object) && !qobject_cast<QToolButton*>(object)) {
        return false;
    }
    // Never replace an unrelated effect (shadows etc.)
    QGraphicsEffect* existing = button->graphicsEffect();
    return !existing || qobject_cast<PressEffect*>(existing);
}

PressEffect* transientEffect(QWidget* widget)
{
    if (auto* effect = qobject_cast<PressEffect*>(widget->graphicsEffect())) {
        return effect;
    }
    auto* effect = new PressEffect(widget);
    widget->setGraphicsEffect(effect);
    return effect;
}

class MotionFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton
                && Theme::motionEnabled() && wantsPressFeedback(watched)) {
                transientEffect(static_cast<QWidget*>(watched))->pressDown();
            }
            break;
        case QEvent::MouseButtonRelease:
            if (auto* widget = qobject_cast<QWidget*>(watched)) {
                if (auto* effect = qobject_cast<PressEffect*>(widget->graphicsEffect())) {
                    effect->release();
                }
            }
            break;
        case QEvent::Show:
            if (auto* widget = qobject_cast<QWidget*>(watched); widget && widget->isWindow()) {
                animateWindowIn(widget);
            }
            break;
        default:
            break;
        }
        return false; // Never swallow anything: motion is decoration on top of behavior
    }

private:
    static void animateWindowIn(QWidget* window)
    {
        if (!Theme::motionEnabled()) {
            return;
        }
        const bool dialog = qobject_cast<QDialog*>(window) != nullptr;
        const bool popup = window->windowType() == Qt::Popup; // menus, combo box lists
        if (!dialog && !popup) {
            return;
        }
        // Nearly invisible right away (no flash), animated once Qt has finished placing it.
        // Not fully 0: Windows lets clicks fall through fully transparent windows, which
        // would close a dropdown opened by a quick click.
        constexpr qreal StartOpacity = 0.02;
        window->setWindowOpacity(StartOpacity);
        const int travel = dialog ? DialogRise : PopupSlide;
        const int ms = dialog ? DialogMs : PopupMs;
        QPointer<QWidget> guard(window);
        QTimer::singleShot(0, window, [guard, travel, ms]() {
            if (!guard) {
                return;
            }
            const QPoint target = guard->pos();
            Theme::animate(guard, StartOpacity, 1.0, ms, [guard, target, travel](qreal v) {
                if (!guard) {
                    return;
                }
                guard->setWindowOpacity(v);
                guard->move(target.x(), target.y() + qRound(travel * (1.0 - v)));
            });
        });
    }
};

} // namespace

namespace Motion {

void install(QApplication& app)
{
    // Qt's own menu/combo effects would double up with ours
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    app.installEventFilter(new MotionFilter(&app));
}

PressEffect* pressEffect(QWidget* widget)
{
    PressEffect* effect = transientEffect(widget);
    effect->setPersistent(true);
    return effect;
}

} // namespace Motion

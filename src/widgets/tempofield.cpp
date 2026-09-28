#include "tempofield.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyle>
#include <QtMath>

namespace {

constexpr int PixelsPerStep = 3;      // Normal drag: 1 BPM every 3 px
constexpr int FinePixelsPerStep = 12; // Shift: 1 BPM every 12 px
constexpr int DragThreshold = 3;      // Below this a press counts as a click (edit)

} // namespace

TempoField::TempoField(QWidget* parent)
    : QSpinBox(parent)
{
    // The line edit gets the mouse first; route it through the drag logic
    lineEdit()->installEventFilter(this);
    lineEdit()->setCursor(Qt::SizeVerCursor);
    lineEdit()->setReadOnly(true); // Typing is entered explicitly (click or double-click)
    setToolTip("Tempo. Drag up or down (Shift for fine), or click to type");
    setProperty("dragging", false);
}

void TempoField::setDragging(bool dragging)
{
    m_dragging = dragging;
    setProperty("dragging", dragging); // Styled highlight while dragging
    style()->unpolish(this);
    style()->polish(this);
}

bool TempoField::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != lineEdit()) {
        return QSpinBox::eventFilter(watched, event);
    }

    // While typing, the line edit behaves normally until Enter or focus loss
    if (!lineEdit()->isReadOnly()) {
        if (event->type() == QEvent::FocusOut
            || (event->type() == QEvent::KeyPress
                && (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Return
                    || static_cast<QKeyEvent*>(event)->key() == Qt::Key_Enter
                    || static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape))) {
            if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                setValue(m_pressValue); // Escape cancels the edit
            }
            interpretText();
            lineEdit()->setReadOnly(true);
            lineEdit()->setCursor(Qt::SizeVerCursor);
            lineEdit()->deselect();
            clearFocus();
            return event->type() == QEvent::KeyPress;
        }
        return QSpinBox::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton) {
            return false;
        }
        m_pressed = true;
        m_pressY = int(e->globalPosition().y());
        m_pressValue = value();
        return true;
    }
    case QEvent::MouseMove: {
        if (!m_pressed) {
            return false;
        }
        auto* e = static_cast<QMouseEvent*>(event);
        const int dy = m_pressY - int(e->globalPosition().y()); // Up = faster
        if (!m_dragging && qAbs(dy) >= DragThreshold) {
            setDragging(true);
        }
        if (m_dragging) {
            const int perStep = (e->modifiers() & Qt::ShiftModifier) ? FinePixelsPerStep : PixelsPerStep;
            setValue(m_pressValue + dy / perStep);
        }
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (!m_pressed) {
            return false;
        }
        m_pressed = false;
        if (m_dragging) {
            setDragging(false);
        } else {
            // A plain click switches to typing
            lineEdit()->setReadOnly(false);
            lineEdit()->setCursor(Qt::IBeamCursor);
            lineEdit()->setFocus();
            selectAll();
        }
        return true;
    }
    case QEvent::MouseButtonDblClick:
        return true; // Handled by the press/release pair above
    default:
        return QSpinBox::eventFilter(watched, event);
    }
}

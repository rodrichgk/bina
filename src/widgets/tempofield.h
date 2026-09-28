#ifndef TEMPOFIELD_H
#define TEMPOFIELD_H

#include <QSpinBox>

// BPM box that works like a hardware encoder: drag up/down to change the tempo
// (Shift for fine steps), click without dragging or double-click to type a value.
class TempoField : public QSpinBox
{
    Q_OBJECT

public:
    explicit TempoField(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setDragging(bool dragging);

    bool m_pressed = false;
    bool m_dragging = false;
    int m_pressY = 0;
    int m_pressValue = 0;
};

#endif // TEMPOFIELD_H

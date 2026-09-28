#ifndef COLORSWATCHPICKER_H
#define COLORSWATCHPICKER_H

#include <QColor>
#include <QWidget>

class QButtonGroup;
class QFrame;
class QPushButton;

// One-click swatches from the theme palette, plus a Custom... picker.
class ColorSwatchPicker : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSwatchPicker(QWidget* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);

private:
    void pickCustom();
    void refresh();

    QColor m_color;
    QList<QColor> m_palette;
    QButtonGroup* m_swatches;
    QPushButton* m_customButton;
    QFrame* m_preview;
};

#endif // COLORSWATCHPICKER_H

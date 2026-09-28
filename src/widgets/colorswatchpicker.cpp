#include "colorswatchpicker.h"
#include "../theme.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

ColorSwatchPicker::ColorSwatchPicker(QWidget* parent)
    : QWidget(parent)
    , m_palette(Theme::clipPalette())
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QHBoxLayout* swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(6);
    m_swatches = new QButtonGroup(this);
    m_swatches->setExclusive(true);
    for (int i = 0; i < m_palette.size(); ++i) {
        const QColor c = m_palette[i];
        QPushButton* swatch = new QPushButton();
        swatch->setCheckable(true);
        swatch->setFixedSize(20, 20);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(c.name().toUpper());
        swatch->setStyleSheet(QString(
            "QPushButton { background: %1; border: 2px solid %1; border-radius: 4px; padding: 0; }"
            "QPushButton:hover { border-color: %2; }"
            "QPushButton:checked { border-color: %3; }")
            .arg(c.name(), Theme::TextDim.name(), Theme::Text.name()));
        m_swatches->addButton(swatch, i);
        swatchRow->addWidget(swatch);
    }
    swatchRow->addStretch();
    connect(m_swatches, &QButtonGroup::idClicked, this, [this](int id) {
        setColor(m_palette.value(id, m_color));
    });

    QHBoxLayout* customRow = new QHBoxLayout();
    customRow->setSpacing(8);
    m_customButton = new QPushButton("Custom...");
    connect(m_customButton, &QPushButton::clicked, this, &ColorSwatchPicker::pickCustom);
    m_preview = new QFrame();
    m_preview->setFixedSize(20, 20);
    customRow->addWidget(m_customButton);
    customRow->addWidget(m_preview);
    customRow->addStretch();

    layout->addLayout(swatchRow);
    layout->addLayout(customRow);

    m_color = m_palette.value(0);
    refresh();
}

void ColorSwatchPicker::setColor(const QColor& color)
{
    if (!color.isValid() || color == m_color) {
        refresh();
        return;
    }
    m_color = color;
    refresh();
    emit colorChanged(m_color);
}

void ColorSwatchPicker::pickCustom()
{
    QColorDialog dialog(m_color, this);
    dialog.setWindowTitle("Track Color");
    for (int i = 0; i < m_palette.size(); ++i) {
        dialog.setCustomColor(i, m_palette[i]);
    }
    if (dialog.exec() == QDialog::Accepted) {
        setColor(dialog.selectedColor());
    }
}

void ColorSwatchPicker::refresh()
{
    m_preview->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 4px;")
                                 .arg(m_color.name(), Theme::Border.name()));

    // Keep the swatch row in sync (custom colors uncheck all swatches)
    const int index = m_palette.indexOf(m_color);
    if (index >= 0) {
        m_swatches->button(index)->setChecked(true);
    } else if (QAbstractButton* checked = m_swatches->checkedButton()) {
        m_swatches->setExclusive(false);
        checked->setChecked(false);
        m_swatches->setExclusive(true);
    }
}

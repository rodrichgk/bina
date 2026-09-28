#include "brandmark.h"
#include "../theme.h"

#include <QPainter>

BrandMark::BrandMark(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void BrandMark::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    // Same panel as the ruler it sits beside
    p.fillRect(rect(), Theme::Surface);
    p.setPen(Theme::Border);
    p.drawLine(0, height() - 1, width(), height() - 1);
    p.setRenderHint(QPainter::Antialiasing);

    // Mark height matches the wordmark's cap height plus a little, as on the brand board
    const qreal markHeight = 15;
    const QRectF markBox(12, (height() - markHeight) / 2.0, markHeight * 76.0 / 94.0, markHeight);
    p.fillPath(Theme::markPath(markBox, 0), Theme::Accent); // Plain b below 24 px, per the logo spec

    const QFont font = Theme::brandFont(11);
    p.setFont(font);
    p.setPen(Theme::Text);
    const QRectF textBox(markBox.right() + 7, 0, width() - markBox.right() - 7, height());
    p.drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft, "bina");
}

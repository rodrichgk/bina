#include "timelineruler.h"
#include "musicalgrid.h"
#include "../core/timescale.h"
#include "../theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

TimelineRuler::TimelineRuler(const ProjectModel* model, const TimeScale* scale, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_scale(scale)
{
    setFixedHeight(Height);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click or drag to move the playhead (Alt: no snapping)");
}

void TimelineRuler::setOffset(int x)
{
    if (m_offset != x) {
        m_offset = x;
        update();
    }
}

void TimelineRuler::setPlayhead(double seconds)
{
    if (!qFuzzyCompare(seconds + 1.0, m_playhead + 1.0)) {
        m_playhead = seconds;
        update();
    }
}

void TimelineRuler::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(rect(), Theme::Surface);

    const double spb = m_model->secondsPerBeat();
    const double pxPerBeat = m_scale->toX(spb);
    const double step = MusicalGrid::stepBeats(pxPerBeat);
    const double pxPerBar = pxPerBeat * ProjectModel::BeatsPerBar;

    // Bar numbers: every bar when there's room, otherwise every 2nd/4th/8th... bar
    int labelEvery = 1;
    while (labelEvery * pxPerBar < 36 && labelEvery < 1024) {
        labelEvery *= 2;
    }

    const QFont font = Theme::monoFont(8);
    const QFontMetrics fm(font);
    p.setFont(font);

    const double firstBeat = std::floor(m_scale->toSeconds(m_offset) / spb / step) * step;
    const double lastBeat = m_scale->toSeconds(m_offset + width()) / spb;
    for (double beat = firstBeat; beat <= lastBeat + step; beat += step) {
        const double x = std::round(m_scale->toX(beat * spb) - m_offset) + 0.5;
        const bool bar = std::fmod(beat, ProjectModel::BeatsPerBar) < 1e-6;
        const bool wholeBeat = std::fmod(beat, 1.0) < 1e-6;
        const int tick = bar ? 10 : wholeBeat ? 6 : 3;
        p.setPen(bar ? Theme::TextFaint.lighter(120) : Theme::TextFaint);
        p.drawLine(QPointF(x, height() - tick), QPointF(x, height()));

        const int barNumber = int(std::round(beat / ProjectModel::BeatsPerBar)) + 1;
        if (bar && (barNumber - 1) % labelEvery == 0) {
            p.setPen(Theme::TextDim);
            p.drawText(QPointF(x + 4, (height() + fm.ascent()) / 2 - 3), QString::number(barNumber));
        }
    }

    // Playhead marker, matching the playhead line below
    const double px = m_scale->toX(m_playhead) - m_offset;
    if (px >= -6 && px <= width() + 6) {
        QPainterPath head;
        head.moveTo(px - 5, height() - 8);
        head.lineTo(px + 5, height() - 8);
        head.lineTo(px, height() - 1);
        head.closeSubpath();
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(head, Theme::Accent);
    }

    p.setPen(Theme::Border);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

double TimelineRuler::secondsAt(int x, Qt::KeyboardModifiers modifiers) const
{
    const double seconds = qMax(0.0, m_scale->toSeconds(x + m_offset));
    if (modifiers & Qt::AltModifier) {
        return seconds;
    }
    const double spb = m_model->secondsPerBeat();
    return MusicalGrid::snapSeconds(seconds, MusicalGrid::stepBeats(m_scale->toX(spb)), spb);
}

void TimelineRuler::mousePressEvent(QMouseEvent* event)
{
    emit seekRequested(secondsAt(int(event->position().x()), event->modifiers()));
}

void TimelineRuler::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        emit seekRequested(secondsAt(int(event->position().x()), event->modifiers())); // Scrub
    }
}

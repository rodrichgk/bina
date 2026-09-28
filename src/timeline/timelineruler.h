#ifndef TIMELINERULER_H
#define TIMELINERULER_H

#include <QWidget>

class ProjectModel;
class TimeScale;

// Bars and beats above the lanes. Stays put when the tracks scroll vertically;
// follows the timeline horizontally. Click or drag on it to move the playhead.
class TimelineRuler : public QWidget
{
    Q_OBJECT

public:
    static constexpr int Height = 26;

    TimelineRuler(const ProjectModel* model, const TimeScale* scale, QWidget* parent = nullptr);

    void setOffset(int x);
    void setPlayhead(double seconds);

signals:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    double secondsAt(int x, Qt::KeyboardModifiers modifiers) const;

    const ProjectModel* m_model;
    const TimeScale* m_scale;
    int m_offset = 0;
    double m_playhead = 0.0;
};

#endif // TIMELINERULER_H

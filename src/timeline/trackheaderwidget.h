#ifndef TRACKHEADERWIDGET_H
#define TRACKHEADERWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>

class TrackHeaderWidget : public QWidget
{
    Q_OBJECT

public:
    TrackHeaderWidget(int trackIndex, const QString& name, QWidget* parent = nullptr);
    int trackIndex() const { return m_trackIndex; }
    
    void setTrackName(const QString& name);
    QString getTrackName() const;
    
    void setMuted(bool muted);
    void setColor(const QColor& color);
    // Second line under the name (e.g. the instrument); hidden when empty
    void setSubtitle(const QString& subtitle);
    bool isMuted() const;

signals:
    void muteToggled(bool muted);
    void settingsRequested(int trackIndex);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onMuteButtonToggled(bool checked);

private:
    void setupUI();
    void styleComponents();
    void refreshMutedLook(bool muted);
    
    int m_trackIndex;
    QColor m_color;
    QHBoxLayout* m_layout;
    QLabel* m_nameLabel;
    QLabel* m_subtitleLabel;
    QPushButton* m_muteButton;
};

#endif // TRACKHEADERWIDGET_H

#ifndef TRANSPORTDOCK_H
#define TRANSPORTDOCK_H

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QButtonGroup>
#include "appconfig.h"
#include <QPointer>
#include <QVariantAnimation>

class PressEffect;

class TransportDock : public QWidget
{
    Q_OBJECT

public:
    explicit TransportDock(QWidget *parent = nullptr);
    
    // Transport control methods
    bool isPlaying() const { return m_isPlaying; }
    bool isRecording() const { return m_isRecording; }
    double getCurrentPosition() const { return m_currentPosition; }
    int getBPM() const;
    
    void setPlaybackPosition(double seconds);
    void setBPM(int bpm);
    void setDuration(double seconds);

public slots:
    void setPlayingState(bool playing);
    void togglePlay();
    void play();
    void stop();
    void pause();
    void record();
    void rewind();
    void fastForward();
    void stopAndReturn();
    
    // Project management slots
    void newProject();
    void openProject();
    void saveProject();
    
    // Add button slot
    void showAddMenu();
    
    // Track management
    void addAudioTrack();
    void addMidiTrack();
    void addInstrumentTrack();

signals:
    // Transport signals
    void playRequested();
    void stopRequested();
    void pauseRequested();
    void recordRequested();
    void stopAndReturnRequested();
    void seekRequested(double seconds);
    void bpmChanged(int bpm);
    
    // Project signals
    void newProjectRequested();
    void openProjectRequested();
    void saveProjectRequested();
    
    // Track signals
    void audioTrackRequested();
    void midiTrackRequested();
    void instrumentTrackRequested();
    void loadAudioFileRequested();

private slots:
    void onPlayStopClicked();
    void onRecordClicked();
    void onPositionSliderChanged(int value);
    void onBPMChanged(int bpm);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void applyModernStyling();
    void refreshTransportIcons();
    
    QString formatTime(double seconds) const;
    void updateTimeDisplay();
    
    // Transport state
    bool m_isPlaying;
    bool m_isRecording;
    double m_currentPosition;
    
    // UI Components - Container
    QWidget* m_dockContainer;
    
    // UI Components - Transport
    QPushButton* m_playStopButton;
    QPushButton* m_stopAndReturnButton;
    QPushButton* m_recordButton;
    QPushButton* m_rewindButton;
    QPushButton* m_fastForwardButton;
    QPushButton* m_addButton;
    
    // UI Components - Time/Position
    QLabel* m_timeLabel;
    QLabel* m_durationLabel;
    double m_duration = 0.0;
    QSlider* m_positionSlider;
    QSpinBox* m_bpmSpinBox;
    QLabel* m_bpmLabel;
    
    // Armed-record pulse
    PressEffect* m_recordOpacity = nullptr;
    QPointer<QVariantAnimation> m_recordPulse;

    // Layout
    QHBoxLayout* m_mainLayout;
};

#endif // TRANSPORTDOCK_H

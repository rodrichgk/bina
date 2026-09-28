#include "transportdock.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QGraphicsDropShadowEffect>
#include "widgets/motion.h"
#include <QPainter>
#include "theme.h"
#include "widgets/tempofield.h"
#include "timeline/musicalgrid.h"

TransportDock::TransportDock(QWidget *parent)
    : QWidget(parent)
    , m_isPlaying(false)
    , m_isRecording(false)
    , m_currentPosition(0.0)
{
    setupUI();
    applyModernStyling();
}

void TransportDock::setupUI() {
    // Outer layout: the dock takes the middle ~60% of the window
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(16, 6, 16, 16); // Room for the painted shadow

    m_dockContainer = new QWidget(this);
    m_dockContainer->setObjectName("dockContainer");
    m_dockContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_dockContainer->setMinimumWidth(560);
    m_dockContainer->setMaximumWidth(980);

    QVBoxLayout* dockLayout = new QVBoxLayout(m_dockContainer);
    dockLayout->setSpacing(0);
    dockLayout->setContentsMargins(10, 4, 10, 4);

    auto makeButton = [this](Theme::Glyph glyph, const QString& tip, int size) {
        QPushButton* b = new QPushButton(this);
        b->setIcon(Theme::icon(glyph, Theme::TextDim));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setFixedSize(size, size);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // ---- Row 1: add on the left, transport buttons centered ----
    QHBoxLayout* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(6);
    
    m_addButton = makeButton(Theme::Glyph::Plus, "Add (import audio, new track)", 30);
    m_addButton->setObjectName("addButton");
    connect(m_addButton, &QPushButton::clicked, this, &TransportDock::showAddMenu);
    
    m_rewindButton = makeButton(Theme::Glyph::SkipBack, "Return to start", 30);
    connect(m_rewindButton, &QPushButton::clicked, this, &TransportDock::rewind);
    
    m_playStopButton = makeButton(Theme::Glyph::Play, "Play", 32);
    m_playStopButton->setObjectName("playButton");
    m_playStopButton->setCheckable(true);
    connect(m_playStopButton, &QPushButton::clicked, this, &TransportDock::onPlayStopClicked);
    
    m_stopAndReturnButton = makeButton(Theme::Glyph::Stop, "Stop and return to start", 30);
    connect(m_stopAndReturnButton, &QPushButton::clicked, this, &TransportDock::stopAndReturn);
    
    m_recordButton = makeButton(Theme::Glyph::Record, "Record", 30);
    m_recordButton->setObjectName("recordButton");
    m_recordButton->setCheckable(true);
    connect(m_recordButton, &QPushButton::clicked, this, &TransportDock::onRecordClicked);
    
    m_fastForwardButton = makeButton(Theme::Glyph::SkipForward, "Forward 10 seconds", 30);
    connect(m_fastForwardButton, &QPushButton::clicked, this, &TransportDock::fastForward);
    
    // Add on the left, tempo on the right: same size and outline, so the dock reads symmetrical
    constexpr int sideWidth = 92;
    constexpr int sideHeight = 30;
    m_addButton->setText("Add");
    m_addButton->setFixedSize(sideWidth, sideHeight);
    m_bpmSpinBox = new TempoField();
    m_bpmSpinBox->setObjectName("tempo");
    m_bpmSpinBox->setRange(20, 400);
    m_bpmSpinBox->setValue(120);
    m_bpmSpinBox->setSuffix(" BPM");
    m_bpmSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_bpmSpinBox->setKeyboardTracking(false); // Apply when typing is done, not at "1", "14", "140"
    m_bpmSpinBox->setAlignment(Qt::AlignCenter);
    m_bpmSpinBox->setFont(Theme::monoFont(9, QFont::Medium));
    m_bpmSpinBox->setFixedSize(sideWidth, sideHeight);
    connect(m_bpmSpinBox, &QSpinBox::valueChanged, this, &TransportDock::onBPMChanged);

    controlsRow->addWidget(m_addButton);
    controlsRow->addStretch();
    controlsRow->addWidget(m_rewindButton);
    controlsRow->addWidget(m_stopAndReturnButton);
    controlsRow->addWidget(m_playStopButton); // Play sits dead center
    controlsRow->addWidget(m_recordButton);
    controlsRow->addWidget(m_fastForwardButton);
    controlsRow->addStretch();
    controlsRow->addWidget(m_bpmSpinBox);
    
    // ---- Row 2: full-width scrub bar ----
    m_positionSlider = new QSlider(Qt::Horizontal);
    m_positionSlider->setObjectName("scrubSlider");
    m_positionSlider->setRange(0, 0);
    m_positionSlider->setEnabled(false); // Enabled once audio is loaded
    m_positionSlider->setToolTip("Drag to move the playhead");
    m_positionSlider->setFocusPolicy(Qt::NoFocus);
    m_positionSlider->setCursor(Qt::PointingHandCursor);
    connect(m_positionSlider, &QSlider::valueChanged, this, &TransportDock::onPositionSliderChanged);
    
    // ---- Row 3: elapsed under the start of the bar, total under the end ----
    QHBoxLayout* timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(2, 0, 2, 0);
    
    m_timeLabel = new QLabel();
    m_timeLabel->setTextFormat(Qt::RichText);
    m_timeLabel->setObjectName("timeLabel");
    m_timeLabel->setFont(Theme::monoFont(9, QFont::Medium));
    
    m_durationLabel = new QLabel(formatTime(0.0));
    m_durationLabel->setObjectName("durationLabel");
    m_durationLabel->setFont(Theme::monoFont(9, QFont::Normal));
    m_durationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    
    timeRow->addWidget(m_timeLabel);
    timeRow->addStretch();
    timeRow->addWidget(m_durationLabel);
    
    dockLayout->addLayout(controlsRow);
    dockLayout->addWidget(m_positionSlider);
    dockLayout->addLayout(timeRow);
    updateTimeDisplay();
    
    // Never let the window squeeze the dock (it would clip the round play button)
    m_dockContainer->setFixedHeight(m_dockContainer->sizeHint().height());

    m_mainLayout->addStretch(1);
    m_mainLayout->addWidget(m_dockContainer, 3);
    m_mainLayout->addStretch(1);
    setLayout(m_mainLayout);

    refreshTransportIcons();
}

void TransportDock::setDuration(double seconds) {
    m_duration = qMax(0.0, seconds);
    m_durationLabel->setText(formatTime(m_duration));

    m_positionSlider->blockSignals(true);
    m_positionSlider->setRange(0, static_cast<int>(m_duration * 100));
    m_positionSlider->blockSignals(false);
    m_positionSlider->setEnabled(m_duration > 0);
}

void TransportDock::setPlayingState(bool playing) {
    // Reflects the engine's real state without emitting requests back
    if (m_isPlaying == playing) {
        return;
    }
    m_isPlaying = playing;
    refreshTransportIcons();
}

void TransportDock::togglePlay() {
    onPlayStopClicked();
}

void TransportDock::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    // Soft shadow under the dock: stacked translucent rounded rects, tinted to the canvas
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF dock = QRectF(m_dockContainer->geometry()).translated(0, 4);
    constexpr int layers = 12;
    for (int i = layers; i >= 1; --i) {
        p.setBrush(QColor(4, 4, 6, 10));
        p.drawRoundedRect(dock.adjusted(-i, -i * 0.5, i, i), 6 + i, 6 + i);
    }
}

void TransportDock::applyModernStyling() {
    // The shadow is painted in paintEvent() rather than with a QGraphicsEffect:
    // a parent effect stops the buttons' own press effects from rendering.

    QString qss = R"(
        QWidget#dockContainer {
            background: @surface;
            border: 1px solid @border;
            border-radius: 6px;
        }

        QWidget#dockContainer QPushButton {
            background: transparent;
            border: none;
            border-radius: 4px;
            padding: 0;
        }
        QWidget#dockContainer QPushButton:hover { background: @hover; }
        QWidget#dockContainer QPushButton:pressed { background: @raised; }

        QWidget#dockContainer QPushButton#playButton {
            background: @raised;
            border: 1px solid @border;
            border-radius: 15px; /* just under half of 32px, Qt drops radii >= half */
        }
        QWidget#dockContainer QPushButton#playButton:hover { background: @hover; }
        QWidget#dockContainer QPushButton#playButton:checked { background: @accent; border-color: @accent; }
        QWidget#dockContainer QPushButton#playButton:checked:hover { background: @accentHover; }

        QWidget#dockContainer QPushButton#recordButton:checked { background: @recordSoft; }

        QWidget#dockContainer QPushButton#addButton {
            background: @raised;
            border: 1px solid @border;
            border-radius: 4px;
            color: @textDim;
            padding: 0 12px 0 8px;
            font-weight: 500;
        }
        QWidget#dockContainer QPushButton#addButton:hover { border-color: @textFaint; color: @text; }
        QWidget#dockContainer QPushButton#addButton:pressed { background: @hover; }
        QSpinBox#tempo {
            background: @raised;
            border: 1px solid @border;
            border-radius: 4px;
            color: @text;
            padding: 3px 4px;
        }
        QSpinBox#tempo:hover { border-color: @textFaint; }
        QSpinBox#tempo[dragging="true"] { border-color: @accent; color: @accent; }
        QLabel#timeLabel { color: @text; }
        QLabel#durationLabel { color: @textFaint; }
    )";
    qss.replace("@surface", Theme::Surface.name())
       .replace("@border", Theme::Border.name())
       .replace("@hover", Theme::Hover.name())
       .replace("@raised", Theme::Raised.name())
       .replace("@accentHover", Theme::Accent.lighter(110).name())
       .replace("@accent", Theme::Accent.name())
       .replace("@recordSoft", Theme::rgba(Theme::Record, 40))
       .replace("@textFaint", Theme::TextFaint.name())
       .replace("@text", Theme::Text.name());
    setStyleSheet(qss);
}

void TransportDock::refreshTransportIcons() {
    m_playStopButton->setIcon(m_isPlaying
        ? Theme::icon(Theme::Glyph::Pause, Theme::AccentText)
        : Theme::icon(Theme::Glyph::Play, Theme::Text));
    m_playStopButton->setToolTip(m_isPlaying ? "Pause" : "Play");
    m_playStopButton->setChecked(m_isPlaying);

    m_recordButton->setIcon(Theme::icon(Theme::Glyph::Record,
        m_isRecording ? Theme::Record : Theme::TextDim));
    m_recordButton->setChecked(m_isRecording);

    // Armed record breathes so it can't be missed; stops the moment it's disarmed
    if (!m_recordOpacity) {
        m_recordOpacity = Motion::pressEffect(m_recordButton); // Also gives it the press bounce
    }
    if (m_isRecording && !m_recordPulse && Theme::motionEnabled()) {
        auto* pulse = new QVariantAnimation(this);
        pulse->setDuration(1400);
        pulse->setStartValue(1.0);
        pulse->setKeyValueAt(0.5, 0.45);
        pulse->setEndValue(1.0);
        pulse->setEasingCurve(QEasingCurve::InOutSine);
        pulse->setLoopCount(-1);
        connect(pulse, &QVariantAnimation::valueChanged, m_recordOpacity,
                [this](const QVariant& v) { m_recordOpacity->setOpacity(v.toReal()); });
        pulse->start(QAbstractAnimation::DeleteWhenStopped);
        m_recordPulse = pulse;
    } else if (!m_isRecording && m_recordPulse) {
        m_recordPulse->stop();
        m_recordOpacity->setOpacity(1.0);
    }
}

// Transport control implementations
void TransportDock::onPlayStopClicked() {
    if (m_isPlaying) {
        stop();
    } else {
        play();
    }
}

void TransportDock::play() {
    m_isPlaying = true;
    refreshTransportIcons();
    emit playRequested();
}

void TransportDock::stop() {
    m_isPlaying = false;
    refreshTransportIcons();
    emit stopRequested();
}

void TransportDock::pause() {
    m_isPlaying = false;
    refreshTransportIcons();
    emit pauseRequested();
}

void TransportDock::onRecordClicked() {
    m_isRecording = !m_isRecording;
    refreshTransportIcons();
    emit recordRequested();
}

void TransportDock::record() {
    m_isRecording = true;
    refreshTransportIcons();
    emit recordRequested();
}

void TransportDock::rewind() {
    setPlaybackPosition(0.0);
    emit seekRequested(0.0);
}

void TransportDock::fastForward() {
    double newPos = m_currentPosition + 10.0;
    if (m_duration > 0) {
        newPos = qMin(newPos, m_duration);
    }
    setPlaybackPosition(newPos);
    emit seekRequested(newPos);
}

void TransportDock::stopAndReturn() {
    m_isPlaying = false;
    refreshTransportIcons();
    
    setPlaybackPosition(0.0);
    emit stopAndReturnRequested();
}

void TransportDock::setPlaybackPosition(double seconds) {
    m_currentPosition = seconds;
    updateTimeDisplay();
    
    m_positionSlider->blockSignals(true);
    m_positionSlider->setValue(static_cast<int>(seconds * 100)); // Convert to slider scale
    m_positionSlider->blockSignals(false);
}

void TransportDock::onPositionSliderChanged(int value) {
    double seconds = value / 100.0; // Convert from slider scale
    m_currentPosition = seconds;
    updateTimeDisplay();
    
    emit seekRequested(seconds);
}

int TransportDock::getBPM() const {
    return m_bpmSpinBox->value();
}

void TransportDock::setBPM(int bpm) {
    const QSignalBlocker blocker(m_bpmSpinBox); // Reflect the model, don't echo back
    m_bpmSpinBox->setValue(bpm);
    updateTimeDisplay();
}

void TransportDock::onBPMChanged(int bpm) {
    updateTimeDisplay(); // Same time, new bar position
    emit bpmChanged(bpm);
}

void TransportDock::updateTimeDisplay() {
    // Musical position first (what you arrange by), clock time dimmer beside it
    const double secondsPerBeat = 60.0 / qMax(1, m_bpmSpinBox->value());
    m_timeLabel->setText(QString("%1&nbsp;&nbsp;<span style='color:%2'>%3</span>")
                             .arg(MusicalGrid::position(m_currentPosition, secondsPerBeat),
                                  Theme::TextFaint.name(), formatTime(m_currentPosition)));
}

QString TransportDock::formatTime(double seconds) const {
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int millisecs = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    
    return QString("%1:%2.%3")
           .arg(minutes, 2, 10, QChar('0'))
           .arg(secs, 2, 10, QChar('0'))
           .arg(millisecs, 3, 10, QChar('0'));
}

// Project management slots
void TransportDock::newProject() {
    emit newProjectRequested();
}

void TransportDock::openProject() {
    emit openProjectRequested();
}

void TransportDock::saveProject() {
    emit saveProjectRequested();
}

// Track management slots
void TransportDock::addAudioTrack() {
    emit audioTrackRequested();
}

void TransportDock::addMidiTrack() {
    emit midiTrackRequested();
}

void TransportDock::addInstrumentTrack() {
    emit instrumentTrackRequested();
}

void TransportDock::showAddMenu() {
    QMenu* addMenu = new QMenu(this);
    
    QAction* audioFileAction = new QAction("Import Audio Files...", this);
    connect(audioFileAction, &QAction::triggered, [this]() {
        emit loadAudioFileRequested();
    });
    addMenu->addAction(audioFileAction);
    
    addMenu->addSeparator();
    
    QAction* trackAction = new QAction("New Track", this);
    connect(trackAction, &QAction::triggered, this, &TransportDock::addAudioTrack);
    addMenu->addAction(trackAction);
    
    // Show menu centered above button
    QPoint menuPos = m_addButton->mapToGlobal(QPoint(0, -addMenu->sizeHint().height() - 10));
    addMenu->exec(menuPos);
    
    addMenu->deleteLater();
}

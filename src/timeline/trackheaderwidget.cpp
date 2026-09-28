#include "trackheaderwidget.h"
#include <QIcon>
#include <QDebug>
#include <QStyle>
#include "../theme.h"

TrackHeaderWidget::TrackHeaderWidget(int trackIndex, const QString& name, QWidget* parent)
    : QWidget(parent)
    , m_trackIndex(trackIndex)
    , m_layout(nullptr)
    , m_nameLabel(nullptr)
    , m_muteButton(nullptr)
{
    setupUI();
    styleComponents();
    
    // Enable styling for custom QWidget subclasses
    setAttribute(Qt::WA_StyledBackground, true);
    
    setTrackName(name);
    refreshMutedLook(false);
}

void TrackHeaderWidget::setupUI()
{
    // Create horizontal layout for controls
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(12, 4, 10, 4);
    m_layout->setSpacing(8);
    
    // Track name, with an optional second line (instrument) underneath
    m_nameLabel = new QLabel("Track 1");
    m_nameLabel->setMinimumWidth(80);
    m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_subtitleLabel = new QLabel();
    m_subtitleLabel->setObjectName("subtitle");
    m_subtitleLabel->hide();
    QVBoxLayout* textColumn = new QVBoxLayout();
    textColumn->setSpacing(1);
    textColumn->addStretch();
    textColumn->addWidget(m_nameLabel);
    textColumn->addWidget(m_subtitleLabel);
    textColumn->addStretch();
    
    // Mute button
    m_muteButton = new QPushButton();
    m_muteButton->setCheckable(true);
    m_muteButton->setFixedSize(26, 24);
    m_muteButton->setIconSize(QSize(16, 16));
    m_muteButton->setCursor(Qt::PointingHandCursor);
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    m_muteButton->setToolTip("Mute track");
    setToolTip("Double-click for track settings");
    
    // Connect signals
    connect(m_muteButton, &QPushButton::toggled, this, &TrackHeaderWidget::onMuteButtonToggled);
    
    // Add to layout
    m_layout->addLayout(textColumn);
    m_layout->addStretch(); // Push mute button to the right
    m_layout->addWidget(m_muteButton);
}

void TrackHeaderWidget::styleComponents()
{
    m_nameLabel->setFont(Theme::uiFont(9, QFont::Medium));
    m_subtitleLabel->setFont(Theme::uiFont(8));
    
    QString qss = R"(
        TrackHeaderWidget {
            background: @surface;
            border-bottom: 1px solid @divider;
            border-left: 3px solid @strip;
        }
        TrackHeaderWidget:hover { background: @raised; }
        QLabel { color: @text; background: transparent; }
        QLabel[muted="true"] { color: @textFaint; }
        QLabel#subtitle { color: @textDim; }
        QPushButton {
            background: transparent;
            border: none;
            border-radius: 4px;
            padding: 0;
        }
        QPushButton:hover { background: rgba(255, 255, 255, 20); }
        QPushButton:checked { background: @accentSoft; }
        QPushButton:checked:hover { background: @accentSoftHover; }
    )";
    // Neutral header; the track color only shows as the strip on the left (dimmed when muted)
    const QColor color = m_color.isValid() ? m_color : Theme::Surface;
    const bool muted = m_muteButton && m_muteButton->isChecked();
    qss.replace("@strip", muted ? Theme::mix(color, Theme::Surface, 0.4).name() : color.name())
       .replace("@surface", Theme::Surface.name())
       .replace("@divider", Theme::Divider.name())
       .replace("@raised", Theme::Raised.name())
       .replace("@hover", Theme::Hover.name())
       .replace("@border", Theme::Border.name())
       .replace("@textFaint", Theme::TextFaint.name())
       .replace("@textDim", Theme::TextDim.name())
       .replace("@text", Theme::Text.name())
       .replace("@accentSoftHover", Theme::rgba(Theme::Accent, 60))
       .replace("@accentSoft", Theme::rgba(Theme::Accent, 36));
    setStyleSheet(qss);
}

void TrackHeaderWidget::setSubtitle(const QString& subtitle)
{
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

void TrackHeaderWidget::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        styleComponents();
    }
}

void TrackHeaderWidget::refreshMutedLook(bool muted)
{
    styleComponents(); // The color strip dims when muted
    // Speaker when audible, crossed-out amber speaker when muted
    m_muteButton->setIcon(muted ? Theme::icon(Theme::Glyph::SpeakerMuted, Theme::Accent)
                                : Theme::icon(Theme::Glyph::Speaker, Theme::TextDim));
    m_muteButton->setToolTip(muted ? "Unmute track" : "Mute track");

    m_nameLabel->setProperty("muted", muted);
    m_nameLabel->style()->unpolish(m_nameLabel);
    m_nameLabel->style()->polish(m_nameLabel);
}

void TrackHeaderWidget::setTrackName(const QString& name)
{
    if (m_nameLabel) {
        m_nameLabel->setText(name);
    }
}

QString TrackHeaderWidget::getTrackName() const
{
    return m_nameLabel ? m_nameLabel->text() : QString();
}

void TrackHeaderWidget::setMuted(bool muted)
{
    // Reflects model state; doesn't echo back as a user toggle
    if (m_muteButton && m_muteButton->isChecked() != muted) {
        m_muteButton->blockSignals(true);
        m_muteButton->setChecked(muted);
        m_muteButton->blockSignals(false);
        refreshMutedLook(muted);
    }
}

bool TrackHeaderWidget::isMuted() const
{
    return m_muteButton ? m_muteButton->isChecked() : false;
}

void TrackHeaderWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    
    emit settingsRequested(m_trackIndex);
    
    QWidget::mouseDoubleClickEvent(event);
}

void TrackHeaderWidget::onMuteButtonToggled(bool checked)
{
    refreshMutedLook(checked);
    emit muteToggled(checked); // The timeline forwards this to the model
}

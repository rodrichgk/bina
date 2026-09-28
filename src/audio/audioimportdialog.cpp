#include "audioimportdialog.h"
#include "../core/projectmodel.h"
#include "../theme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QIcon colorDot(const QColor& color)
{
    QPixmap pm(QSize(12, 12) * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1, 1, 10, 10), 3, 3);
    return QIcon(pm);
}

QString formatSize(qint64 bytes)
{
    return QString::number(bytes / 1024.0 / 1024.0, 'f', 1) + " MB";
}

} // namespace

AudioImportDialog::AudioImportDialog(const QStringList& filePaths, const ProjectModel* model, QWidget* parent)
    : QDialog(parent)
    , m_filePaths(filePaths)
    , m_model(model)
{
    const int count = m_filePaths.size();
    setWindowTitle(count == 1 ? "Import Audio File" : QString("Import %1 Audio Files").arg(count));
    setModal(true);
    setMinimumWidth(440);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    // Files: a single file shows its size, several show the first few names
    QGroupBox* fileGroup = new QGroupBox(count == 1 ? "File" : "Files");
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);
    qint64 totalBytes = 0;
    for (const QString& path : m_filePaths) {
        totalBytes += QFileInfo(path).size();
    }
    QString text;
    if (count == 1) {
        text = QString("<b>%1</b><br>%2").arg(QFileInfo(m_filePaths.first()).fileName().toHtmlEscaped(),
                                             formatSize(totalBytes));
    } else {
        constexpr int shown = 5;
        QStringList names;
        for (int i = 0; i < qMin(count, shown); ++i) {
            names << QFileInfo(m_filePaths[i]).fileName().toHtmlEscaped();
        }
        if (count > shown) {
            names << QString("and %1 more").arg(count - shown);
        }
        text = QString("<b>%1 files</b>, %2<br>%3").arg(count).arg(formatSize(totalBytes), names.join("<br>"));
    }
    QLabel* fileInfoLabel = new QLabel(text);
    fileInfoLabel->setWordWrap(true);
    fileLayout->addWidget(fileInfoLabel);
    mainLayout->addWidget(fileGroup);

    // Placement
    QGroupBox* settingsGroup = new QGroupBox("Placement");
    QGridLayout* settingsLayout = new QGridLayout(settingsGroup);
    settingsLayout->setVerticalSpacing(8);
    int row = 0;

    if (count > 1) {
        m_perFileRadio = new QRadioButton("One track per file");
        QRadioButton* sameTrackRadio = new QRadioButton("All on one track, in order");
        m_perFileRadio->setChecked(true);
        QButtonGroup* modeGroup = new QButtonGroup(this);
        modeGroup->addButton(m_perFileRadio);
        modeGroup->addButton(sameTrackRadio);
        connect(m_perFileRadio, &QRadioButton::toggled, this, &AudioImportDialog::updateHint);
        settingsLayout->addWidget(m_perFileRadio, row++, 0, 1, 2);
        settingsLayout->addWidget(sameTrackRadio, row++, 0, 1, 2);
    }

    m_trackLabel = new QLabel();
    m_trackComboBox = new QComboBox();
    for (int i = 0; i < model->trackCount(); ++i) {
        const TrackData& track = model->track(i);
        m_trackComboBox->addItem(colorDot(track.color), track.name, i);
    }
    connect(m_trackComboBox, &QComboBox::currentIndexChanged, this, &AudioImportDialog::updateHint);
    settingsLayout->addWidget(m_trackLabel, row, 0);
    settingsLayout->addWidget(m_trackComboBox, row++, 1);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QString("color: %1;").arg(Theme::TextFaint.name()));
    settingsLayout->addWidget(m_hint, row, 1);
    settingsLayout->setColumnStretch(1, 1);
    mainLayout->addWidget(settingsGroup);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    QPushButton* cancelButton = new QPushButton("Cancel");
    QPushButton* okButton = new QPushButton("Import");
    okButton->setDefault(true);
    okButton->setProperty("primary", true);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(okButton);
    mainLayout->addLayout(buttonLayout);

    updateHint();
}

void AudioImportDialog::updateHint()
{
    const ImportSettings settings = getImportSettings();
    const int count = m_filePaths.size();

    if (count > 1 && settings.oneTrackPerFile) {
        m_trackLabel->setText("Starting at:");
        const int missing = settings.targetTrack + count - m_model->trackCount();
        m_hint->setText(missing > 0
            ? QString("%1 new tracks will be added. Clips take their track's color and effects.").arg(missing)
            : QString("Clips take their track's color and effects."));
    } else {
        m_trackLabel->setText("Track:");
        m_hint->setText(count > 1
            ? QString("Placed one after another, in the order listed. They take the track's color and effects.")
            : QString("The clip takes the track's color and effects."));
    }
}

AudioImportDialog::ImportSettings AudioImportDialog::getImportSettings() const
{
    ImportSettings settings;
    settings.targetTrack = m_trackComboBox->currentData().toInt();
    settings.oneTrackPerFile = m_perFileRadio && m_perFileRadio->isChecked();
    return settings;
}

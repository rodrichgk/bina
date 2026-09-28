#include "tracksettingsdialog.h"
#include <QApplication>
#include <QDebug>
#include "../audio/effects.h"
#include "../widgets/colorswatchpicker.h"
#include "../theme.h"
#include "../audio/audiodecoder.h"
#include "../audio/audioengine.h"
#include "../audio/instruments/instrumentapi.h"

#include <QCheckBox>

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

TrackSettingsDialog::TrackSettingsDialog(ProjectModel* model, int trackIndex, QWidget* parent)
    : QDialog(parent)
    , m_model(model)
    , m_trackIndex(trackIndex)
{
    const TrackData& track = m_model->track(m_trackIndex);
    m_originalVolume = track.volume;
    m_originalPan = track.pan;
    m_originalColor = track.color;
    m_originalInstrument = track.instrument;
    m_instrument = track.instrument;
        
    setWindowTitle(QString("%1 Settings").arg(track.name));
    setModal(true);
    setMinimumWidth(820);
    
    setupUI();
    
    // Load current track settings
    m_trackNameEdit->setText(track.name);
    m_volumeSlider->setValue(qRound(track.volume * 100));
    m_panDial->setValue(qRound(track.pan * 50 + 50)); // -1..1 to 0..100
    m_muteButton->setChecked(track.muted);
    m_soloButton->setChecked(track.soloed);
    m_colorPicker->setColor(track.color);
    m_effectsList->addItems(track.effects);
        
    updateVolumeLabel(m_volumeSlider->value());
    updatePanLabel(m_panDial->value());
}

void TrackSettingsDialog::reject()
{
    // Undo the live previews
    m_model->setTrackVolume(m_trackIndex, m_originalVolume);
    m_model->setTrackPan(m_trackIndex, m_originalPan);
    m_model->setTrackColor(m_trackIndex, m_originalColor);
    m_model->setTrackInstrument(m_trackIndex, m_originalInstrument);
    QDialog::reject();
}

void TrackSettingsDialog::setupUI()
{
    // Two columns so the dialog fits on a laptop screen:
    // what the track is (name, color, mixer) | how it sounds (instrument, effects)
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    QHBoxLayout* columns = new QHBoxLayout();
    columns->setSpacing(16);
    QVBoxLayout* left = new QVBoxLayout();
    left->setSpacing(12);
    left->addWidget(createTrackInfoGroup());
    left->addWidget(createMixerGroup());
    left->addStretch();
    QVBoxLayout* right = new QVBoxLayout();
    right->setSpacing(12);
    right->addWidget(createInstrumentGroup()); // Plays this track's note clips
    right->addWidget(createEffectsGroup());
    right->addStretch();
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);

    mainLayout->addLayout(columns);
    mainLayout->addLayout(createButtonLayout());
}

QGroupBox* TrackSettingsDialog::createTrackInfoGroup()
{
    QGroupBox* group = new QGroupBox("Track Information");
    QGridLayout* layout = new QGridLayout(group);
    
    // Track name
    layout->addWidget(new QLabel("Name:"), 0, 0);
    m_trackNameEdit = new QLineEdit();
    connect(m_trackNameEdit, &QLineEdit::textChanged, this, &TrackSettingsDialog::onTrackNameChanged);
    layout->addWidget(m_trackNameEdit, 0, 1);
    
    // Track color: every clip on the track is drawn in it
    layout->addWidget(new QLabel("Color:"), 1, 0, Qt::AlignTop);
    m_colorPicker = new ColorSwatchPicker();
    connect(m_colorPicker, &ColorSwatchPicker::colorChanged, this, [this](const QColor& color) {
        m_model->setTrackColor(m_trackIndex, color);
    });
    layout->addWidget(m_colorPicker, 1, 1);
    
    return group;
}

namespace {

// Continuous parameters use a 0..1000 slider. "Logarithmic" ones travel exponentially
// (or on a cubic curve when the range starts at 0), so short times and low
// frequencies get as much slider travel as long/high ones.
constexpr int SliderSteps = 1000;

double sliderToValue(const InstrumentParam& p, int step)
{
    const double t = double(step) / SliderSteps;
    if (p.logarithmic && p.min > 0) {
        return p.min * std::pow(p.max / p.min, t);
    }
    if (p.logarithmic) {
        return p.min + (p.max - p.min) * t * t * t;
    }
    return p.min + (p.max - p.min) * t;
}

int valueToSlider(const InstrumentParam& p, double value)
{
    value = qBound(p.min, value, p.max);
    double t;
    if (p.logarithmic && p.min > 0) {
        t = std::log(value / p.min) / std::log(p.max / p.min);
    } else if (p.logarithmic) {
        t = std::cbrt((value - p.min) / qMax(1e-12, p.max - p.min));
    } else {
        t = (value - p.min) / qMax(1e-12, p.max - p.min);
    }
    return qRound(t * SliderSteps);
}

QString formatValue(const InstrumentParam& p, double value)
{
    const double shown = value * p.displayScale;
    if (p.unit == "Hz" && shown >= 1000) {
        return QString("%1 kHz").arg(shown / 1000.0, 0, 'f', 1);
    }
    const QString number = QString::number(shown, 'f', p.decimals);
    return p.unit.isEmpty() ? number : (p.unit == "%" ? number + "%" : number + " " + p.unit);
}

} // namespace

QGroupBox* TrackSettingsDialog::createInstrumentGroup()
{
    QGroupBox* group = new QGroupBox("Instrument");
    QVBoxLayout* layout = new QVBoxLayout(group);
    layout->setSpacing(8);

    // Type: every registered instrument
    QHBoxLayout* typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel("Type:"));
    m_instrumentType = new QComboBox();
    for (const InstrumentDefinition* def : InstrumentRegistry::all()) {
        m_instrumentType->addItem(def->name, def->id);
    }
    const int current = m_instrumentType->findData(m_instrument.type);
    m_instrumentType->setCurrentIndex(qMax(0, current));
    typeRow->addWidget(m_instrumentType, 1);
    layout->addLayout(typeRow);

    m_instrumentDescription = new QLabel();
    m_instrumentDescription->setWordWrap(true);
    m_instrumentDescription->setStyleSheet(QString("color: %1;").arg(Theme::TextFaint.name()));
    layout->addWidget(m_instrumentDescription);

    // Sample row, only for instruments that play a loaded file
    m_samplerRow = new QWidget();
    QHBoxLayout* samplerLayout = new QHBoxLayout(m_samplerRow);
    samplerLayout->setContentsMargins(0, 0, 0, 0);
    m_loadSampleButton = new QPushButton("Load Sample...");
    m_sampleLabel = new QLabel();
    m_sampleLabel->setStyleSheet(QString("color: %1;").arg(Theme::TextDim.name()));
    samplerLayout->addWidget(m_loadSampleButton);
    samplerLayout->addWidget(m_sampleLabel, 1);
    layout->addWidget(m_samplerRow);
    connect(m_loadSampleButton, &QPushButton::clicked, this, &TrackSettingsDialog::loadSample);

    // Parameter controls live in their own panel so a type change can rebuild them
    m_paramPanel = new QWidget();
    layout->addWidget(m_paramPanel);

    connect(m_instrumentType, &QComboBox::currentIndexChanged, this, [this]() {
        m_instrument.type = m_instrumentType->currentData().toString();
        rebuildParamControls();
        applyInstrument();
    });

    m_instrument.type = m_instrumentType->currentData().toString();
    rebuildParamControls();
    return group;
}

void TrackSettingsDialog::rebuildParamControls()
{
    const InstrumentDefinition* def = InstrumentRegistry::find(m_instrument.type);

    // Replace the whole panel: simplest way to drop every old control and connection
    QWidget* panel = new QWidget();
    QFormLayout* form = new QFormLayout(panel);
    form->setContentsMargins(0, 4, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    delete m_paramPanel->parentWidget()->layout()->replaceWidget(m_paramPanel, panel);
    // Deleted now, not deleteLater(): deferred deletes don't run inside this modal dialog's
    // event loop, and the stale panel would sit invisibly on top of the Type dropdown
    delete m_paramPanel;
    m_paramPanel = panel;

    m_instrumentDescription->setText(def ? def->description : QString());
    updateSampleRow();
    if (!def) {
        return;
    }

    for (const InstrumentParam& p : def->params) {
        const double value = m_instrument.params.value(p.id, p.defaultValue);
        const QString id = p.id;
        switch (p.kind) {
        case InstrumentParam::Kind::Choice: {
            QComboBox* combo = new QComboBox();
            combo->addItems(p.choices);
            combo->setCurrentIndex(qBound(0, int(value + 0.5), int(p.choices.size()) - 1));
            connect(combo, &QComboBox::currentIndexChanged, this, [this, id](int index) {
                m_instrument.params.insert(id, index);
                applyInstrument();
            });
            form->addRow(p.label + ":", combo);
            break;
        }
        case InstrumentParam::Kind::Toggle: {
            QCheckBox* check = new QCheckBox();
            check->setChecked(value >= 0.5);
            connect(check, &QCheckBox::toggled, this, [this, id](bool on) {
                m_instrument.params.insert(id, on ? 1.0 : 0.0);
                applyInstrument();
            });
            form->addRow(p.label + ":", check);
            break;
        }
        case InstrumentParam::Kind::Continuous: {
            QWidget* row = new QWidget();
            QHBoxLayout* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            QSlider* slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, SliderSteps);
            slider->setValue(valueToSlider(p, value));
            QLabel* readout = new QLabel(formatValue(p, value));
            readout->setFont(Theme::monoFont(8.5));
            readout->setFixedWidth(70);
            readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            connect(slider, &QSlider::valueChanged, this, [this, p, readout](int step) {
                const double v = sliderToValue(p, step);
                readout->setText(formatValue(p, v));
                m_instrument.params.insert(p.id, v);
                applyInstrument();
            });
            rowLayout->addWidget(slider, 1);
            rowLayout->addWidget(readout);
            form->addRow(p.label + ":", row);
            break;
        }
        }
    }
}

void TrackSettingsDialog::updateSampleRow()
{
    const InstrumentDefinition* def = InstrumentRegistry::find(m_instrument.type);
    m_samplerRow->setVisible(def && def->usesSample);
    m_sampleLabel->setText(m_instrument.samplePath.isEmpty()
        ? QString("No sample loaded")
        : QFileInfo(m_instrument.samplePath).fileName());
}

void TrackSettingsDialog::applyInstrument()
{
    m_model->setTrackInstrument(m_trackIndex, m_instrument); // Live: heard on the next note
}

void TrackSettingsDialog::loadSample()
{
    const QString path = QFileDialog::getOpenFileName(this, "Load Sample", "", AudioDecoder::fileDialogFilter());
    if (path.isEmpty()) {
        return;
    }
    m_loadSampleButton->setEnabled(false);
    m_loadSampleButton->setText("Loading...");

    auto* watcher = new QFutureWatcher<DecodeResult>(this);
    connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [this, watcher, path]() {
        const DecodeResult result = watcher->result();
        watcher->deleteLater();
        m_loadSampleButton->setEnabled(true);
        m_loadSampleButton->setText("Load Sample...");
        if (!result.ok) {
            QMessageBox::warning(this, "Load Sample", QString("Could not load the sample:\n%1").arg(result.error));
            return;
        }
        m_instrument.sample = result.audio;
        m_instrument.samplePath = path;
        updateSampleRow();
        applyInstrument();
    });
    watcher->setFuture(QtConcurrent::run(&AudioDecoder::decode, path, int(AudioEngine::SampleRate)));
}

QGroupBox* TrackSettingsDialog::createMixerGroup()
{
    QGroupBox* group = new QGroupBox("Mixer Controls");
    QGridLayout* layout = new QGridLayout(group);
    
    // Volume fader
    layout->addWidget(new QLabel("Volume:"), 0, 0);
    m_volumeSlider = new QSlider(Qt::Vertical);
    m_volumeSlider->setRange(0, 150); // 0% to 150%
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedHeight(120);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &TrackSettingsDialog::onVolumeChanged);
    
    m_volumeLabel = new QLabel("100%");
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    
    QVBoxLayout* volumeLayout = new QVBoxLayout();
    volumeLayout->addWidget(m_volumeSlider);
    volumeLayout->addWidget(m_volumeLabel);
    layout->addLayout(volumeLayout, 1, 0);
    
    // Pan control
    layout->addWidget(new QLabel("Pan:"), 0, 1);
    m_panDial = new QDial();
    m_panDial->setRange(0, 100); // 0 = full left, 50 = center, 100 = full right
    m_panDial->setValue(50);
    m_panDial->setFixedSize(80, 80);
    connect(m_panDial, &QDial::valueChanged, this, &TrackSettingsDialog::onPanChanged);
    
    m_panLabel = new QLabel("Center");
    m_panLabel->setAlignment(Qt::AlignCenter);
    
    QVBoxLayout* panLayout = new QVBoxLayout();
    panLayout->addWidget(m_panDial);
    panLayout->addWidget(m_panLabel);
    layout->addLayout(panLayout, 1, 1);
    
    // Mute/Solo buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_muteButton = new QPushButton("Mute");
    m_muteButton->setCheckable(true);
    
    m_soloButton = new QPushButton("Solo");
    m_soloButton->setCheckable(true);
    
    buttonLayout->addWidget(m_muteButton);
    buttonLayout->addWidget(m_soloButton);
    layout->addLayout(buttonLayout, 2, 0, 1, 2);
    
    return group;
}

QGroupBox* TrackSettingsDialog::createEffectsGroup()
{
    QGroupBox* group = new QGroupBox("Effects Chain");
    QVBoxLayout* layout = new QVBoxLayout(group);
    
    QLabel* note = new QLabel("Processed top to bottom, on this track's audio only.");
    note->setStyleSheet(QString("color: %1;").arg(Theme::TextFaint.name()));
    layout->addWidget(note);
    
    // Effects list
    m_effectsList = new QListWidget();
    m_effectsList->setMaximumHeight(120);
    connect(m_effectsList, &QListWidget::itemSelectionChanged, this, &TrackSettingsDialog::onEffectSelectionChanged);
    layout->addWidget(m_effectsList);
    
    // Add/Remove controls
    QHBoxLayout* effectsControlLayout = new QHBoxLayout();
    
    m_availableEffects = new QComboBox();
    populateAvailableEffects();
    
    m_addEffectButton = new QPushButton("Add Effect");
    connect(m_addEffectButton, &QPushButton::clicked, this, &TrackSettingsDialog::onAddEffectClicked);
    
    m_removeEffectButton = new QPushButton("Remove Effect");
    m_removeEffectButton->setEnabled(false);
    connect(m_removeEffectButton, &QPushButton::clicked, this, &TrackSettingsDialog::onRemoveEffectClicked);
    
    effectsControlLayout->addWidget(m_availableEffects);
    effectsControlLayout->addWidget(m_addEffectButton);
    effectsControlLayout->addWidget(m_removeEffectButton);
    
    layout->addLayout(effectsControlLayout);
    
    return group;
}

QHBoxLayout* TrackSettingsDialog::createButtonLayout()
{
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_applyButton = new QPushButton("Apply");
    m_cancelButton = new QPushButton("Cancel");
    m_okButton = new QPushButton("OK");
    m_okButton->setDefault(true);
    
    connect(m_applyButton, &QPushButton::clicked, this, &TrackSettingsDialog::applyChanges);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okButton, &QPushButton::clicked, [this]() {
        applyChanges();
        accept();
    });
    
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_okButton);
    
    return buttonLayout;
}

void TrackSettingsDialog::onVolumeChanged(int value)
{
    updateVolumeLabel(value);
    m_model->setTrackVolume(m_trackIndex, value / 100.0f);
}

void TrackSettingsDialog::onPanChanged(int value)
{
    updatePanLabel(value);
    m_model->setTrackPan(m_trackIndex, (value - 50) / 50.0f); // 0..100 to -1..1
}

void TrackSettingsDialog::onTrackNameChanged()
{
    // Applied on OK / Apply
}

void TrackSettingsDialog::onAddEffectClicked()
{
    QString effectName = m_availableEffects->currentText();
    if (!effectName.isEmpty()) {
        m_effectsList->addItem(effectName);
    }
}

void TrackSettingsDialog::onRemoveEffectClicked()
{
    int currentRow = m_effectsList->currentRow();
    if (currentRow >= 0) {
        QListWidgetItem* item = m_effectsList->takeItem(currentRow);
        delete item;
    }
}

void TrackSettingsDialog::onEffectSelectionChanged()
{
    m_removeEffectButton->setEnabled(m_effectsList->currentItem() != nullptr);
}

void TrackSettingsDialog::updateVolumeLabel(int value)
{
    m_volumeLabel->setText(QString("%1%").arg(value));
}

void TrackSettingsDialog::updatePanLabel(int value)
{
    if (value < 45) {
        m_panLabel->setText(QString("L%1").arg(50 - value));
    } else if (value > 55) {
        m_panLabel->setText(QString("R%1").arg(value - 50));
    } else {
        m_panLabel->setText("Center");
    }
}

void TrackSettingsDialog::populateAvailableEffects()
{
    m_availableEffects->addItems(Effects::available());
}

void TrackSettingsDialog::applyChanges()
{
    const QString name = m_trackNameEdit->text().trimmed();
    if (!name.isEmpty()) {
        m_model->setTrackName(m_trackIndex, name);
    }
    m_model->setTrackMuted(m_trackIndex, m_muteButton->isChecked());
    m_model->setTrackSoloed(m_trackIndex, m_soloButton->isChecked());
    
    QStringList effects;
    for (int i = 0; i < m_effectsList->count(); ++i) {
        effects << m_effectsList->item(i)->text();
    }
    m_model->setTrackEffects(m_trackIndex, effects);
    
    // Applied values become the new baseline for Cancel
    m_originalVolume = m_model->track(m_trackIndex).volume;
    m_originalPan = m_model->track(m_trackIndex).pan;
    m_originalColor = m_model->track(m_trackIndex).color;
    m_originalInstrument = m_model->track(m_trackIndex).instrument;
}

#ifndef TRACKSETTINGSDIALOG_H
#define TRACKSETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include "../core/projectmodel.h"

class ColorSwatchPicker;

class TrackSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // Edits one track of the model. Color, volume and pan apply live; Cancel restores them.
    TrackSettingsDialog(ProjectModel* model, int trackIndex, QWidget* parent = nullptr);

public slots:
    void reject() override;

private slots:
    void onVolumeChanged(int value);
    void onPanChanged(int value);
    void onTrackNameChanged();
    void onAddEffectClicked();
    void onRemoveEffectClicked();
    void onEffectSelectionChanged();

private:
    void setupUI();
    QGroupBox* createTrackInfoGroup();
    QGroupBox* createMixerGroup();
    QGroupBox* createInstrumentGroup();
    void applyInstrument();      // Working copy -> model (live)
    void rebuildParamControls(); // Controls for the selected instrument type's parameters
    void updateSampleRow();
    void loadSample();
    QGroupBox* createEffectsGroup();
    QHBoxLayout* createButtonLayout();
    void updateVolumeLabel(int value);
    void updatePanLabel(int value);
    void populateAvailableEffects();
    void applyChanges();
    
    ProjectModel* m_model;
    int m_trackIndex;
    float m_originalVolume;
    float m_originalPan;
    QColor m_originalColor;
    InstrumentSettings m_originalInstrument;
    InstrumentSettings m_instrument; // working copy, holds the loaded sample
    bool m_loadingInstrument = false;
    
    // Track Info
    QLineEdit* m_trackNameEdit;
    ColorSwatchPicker* m_colorPicker;
    
    // Instrument: controls are generated from the instrument's declared parameters
    QComboBox* m_instrumentType = nullptr;
    QLabel* m_instrumentDescription = nullptr;
    QWidget* m_samplerRow = nullptr;
    QPushButton* m_loadSampleButton = nullptr;
    QLabel* m_sampleLabel = nullptr;
    QWidget* m_paramPanel = nullptr;

    // Mixer Controls
    QSlider* m_volumeSlider;
    QLabel* m_volumeLabel;
    QDial* m_panDial;
    QLabel* m_panLabel;
    QPushButton* m_muteButton;
    QPushButton* m_soloButton;
    
    // Effects Chain
    QListWidget* m_effectsList;
    QComboBox* m_availableEffects;
    QPushButton* m_addEffectButton;
    QPushButton* m_removeEffectButton;
    
    // Dialog Buttons
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QPushButton* m_applyButton;
};

#endif // TRACKSETTINGSDIALOG_H

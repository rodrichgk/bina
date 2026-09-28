#ifndef AUDIOIMPORTDIALOG_H
#define AUDIOIMPORTDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QRadioButton;
class ProjectModel;

// Asks where imported files go. Clips take their track's color and effects.
// With several files the user picks one track per file, or all in order on one track.
class AudioImportDialog : public QDialog
{
    Q_OBJECT

public:
    struct ImportSettings {
        int targetTrack = 0;        // first track (per-file mode) or the only track
        bool oneTrackPerFile = true;
    };

    AudioImportDialog(const QStringList& filePaths, const ProjectModel* model, QWidget* parent = nullptr);

    ImportSettings getImportSettings() const;

private:
    void updateHint();

    QStringList m_filePaths;
    const ProjectModel* m_model;
    QComboBox* m_trackComboBox;
    QLabel* m_trackLabel;
    QLabel* m_hint;
    QRadioButton* m_perFileRadio = nullptr;
};

#endif // AUDIOIMPORTDIALOG_H

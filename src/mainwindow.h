#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <functional>

#include "core/projectmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class AudioEngine;
class ProjectModel;
class TimelineWidget;
class TransportDock;
class PianoRollWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Replaces the current project with a .bina file (asks first if there are unsaved changes)
    bool openProject(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewProjectRequested();
    void openProjectDialog();
    bool saveProject();
    bool saveProjectAs();
    void onAudioTrackRequested();
    void openPianoRoll(int clipId);
    void seekTo(double seconds);
    void loadAudioFile();
    void onFilesDropped(const QStringList& filePaths, int track, double seconds);

private:
    void setupMenuBar();
    struct ImportJob {
        QString filePath;
        int track;
        double start; // seconds; negative = after the last clip on the track (resolved at commit)
    };
    // Decodes every file on worker threads, then adds the clips to the model in job order
    // `rejected` lists files already refused (e.g. unsupported type) so they show in the summary
    // `onFinished` replaces the usual summary (used when opening a project)
    void importFiles(const QVector<ImportJob>& jobs, const QStringList& rejected = {},
                     std::function<void(int imported, const QStringList& errors)> onFinished = {});
    // `index`, adding tracks first if it's past the last one
    int trackOrNew(int index);

    // Project file state
    bool maybeSave(); // false = the user cancelled
    bool writeProject(const QString& path);
    void resetToProject(const QVector<TrackData>& tracks, double tempo);
    QVector<TrackData> defaultTracks() const;
    void setProjectPath(const QString& path);
    void setModified(bool modified);
    void closePianoRolls();
    void loadStepFinished(); // one async part of opening a project is done

    Ui::MainWindow *ui;
    ProjectModel *m_model;
    AudioEngine *m_audioEngine;
    TimelineWidget *m_timelineWidget;
    TransportDock *m_transportDock;
    QHash<int, QPointer<PianoRollWindow>> m_pianoRolls; // by clip id

    QString m_projectPath;     // empty = never saved
    int m_projectGeneration = 0; // bumped on every reset, so late async results are dropped
    bool m_loading = false;    // edits made by opening a project don't count as changes
    int m_pendingLoadSteps = 0;
    QStringList m_loadProblems;
};
#endif // MAINWINDOW_H

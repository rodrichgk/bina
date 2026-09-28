#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QVector>

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

private slots:
    void onNewProjectRequested();
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
    void importFiles(const QVector<ImportJob>& jobs, const QStringList& rejected = {});
    // `index`, adding tracks first if it's past the last one
    int trackOrNew(int index);

    Ui::MainWindow *ui;
    ProjectModel *m_model;
    AudioEngine *m_audioEngine;
    TimelineWidget *m_timelineWidget;
    TransportDock *m_transportDock;
    QHash<int, QPointer<PianoRollWindow>> m_pianoRolls; // by clip id
};
#endif // MAINWINDOW_H

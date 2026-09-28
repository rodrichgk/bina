#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "appconfig.h"
#include "audio/audiodecoder.h"
#include "audio/audioengine.h"
#include "audio/audioimportdialog.h"
#include "core/projectmodel.h"
#include "pianoroll/pianoroll.h"
#include "theme.h"
#include "timeline/timelinewidget.h"
#include "transportdock.h"

#include <QBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// Default track colors cycle through the theme palette
QColor defaultTrackColor(int index)
{
    const QList<QColor> palette = Theme::clipPalette();
    return palette[index % palette.size()];
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // The project model is the single source of truth; the timeline and engine both observe it
    m_model = new ProjectModel(this);
    const AppConfig& config = AppConfig::instance();
    const int defaultTracks = (config.getSceneHeight() - config.getTrackHeight()) / config.getTrackHeight();
    for (int i = 0; i < defaultTracks; ++i) {
        m_model->addTrack(QString("Track %1").arg(i + 1), defaultTrackColor(i));
    }

    m_audioEngine = new AudioEngine(m_model, this);
    m_timelineWidget = new TimelineWidget(m_model, this);
    m_transportDock = new TransportDock(this);

    QVBoxLayout *layout = new QVBoxLayout(ui->centralwidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_timelineWidget, 1);
    layout->addWidget(m_transportDock);

    // Transport -> engine
    connect(m_transportDock, &TransportDock::playRequested, m_audioEngine, &AudioEngine::onTransportPlay);
    connect(m_transportDock, &TransportDock::stopRequested, m_audioEngine, &AudioEngine::onTransportStop);
    connect(m_transportDock, &TransportDock::stopAndReturnRequested, m_audioEngine, &AudioEngine::onTransportStopAndReturn);
    connect(m_transportDock, &TransportDock::seekRequested, m_audioEngine, &AudioEngine::onPositionChanged);

    // Transport -> project actions
    connect(m_transportDock, &TransportDock::newProjectRequested, this, &MainWindow::onNewProjectRequested);
    connect(m_transportDock, &TransportDock::audioTrackRequested, this, &MainWindow::onAudioTrackRequested);
    connect(m_transportDock, &TransportDock::instrumentTrackRequested, this, &MainWindow::onAudioTrackRequested);
    connect(m_transportDock, &TransportDock::midiTrackRequested, this, &MainWindow::onAudioTrackRequested);
    connect(m_transportDock, &TransportDock::loadAudioFileRequested, this, &MainWindow::loadAudioFile);

    // Keep playhead, dock readout and scrub bar in sync whichever one moves
    connect(m_timelineWidget, &TimelineWidget::indicatorPositionChanged, m_audioEngine, &AudioEngine::onPositionChanged);
    connect(m_timelineWidget, &TimelineWidget::indicatorPositionChanged, m_transportDock, &TransportDock::setPlaybackPosition);
    connect(m_transportDock, &TransportDock::seekRequested, m_timelineWidget, &TimelineWidget::setIndicatorPosition);
    connect(m_audioEngine, &AudioEngine::positionChanged, m_transportDock, &TransportDock::setPlaybackPosition);
    connect(m_audioEngine, &AudioEngine::positionChanged, m_timelineWidget, &TimelineWidget::setIndicatorPosition);

    // Engine owns play state; the model owns project length
    connect(m_audioEngine, &AudioEngine::playbackStateChanged, m_transportDock, &TransportDock::setPlayingState);
    connect(m_audioEngine, &AudioEngine::playbackStateChanged, m_timelineWidget, &TimelineWidget::setPlaybackMode);
    connect(m_model, &ProjectModel::lengthChanged, m_transportDock, &TransportDock::setDuration);

    // Drag & drop onto the timeline
    connect(m_timelineWidget, &TimelineWidget::filesDropped, this, &MainWindow::onFilesDropped);

    // Note clips open in the piano roll; open piano rolls follow the song position
    connect(m_timelineWidget, &TimelineWidget::clipEditRequested, this, &MainWindow::openPianoRoll);
    connect(m_audioEngine, &AudioEngine::positionChanged, this, [this](double seconds) {
        for (const QPointer<PianoRollWindow>& window : std::as_const(m_pianoRolls)) {
            if (window) {
                window->setSongPosition(seconds);
            }
        }
    });

    // Tempo: dock <-> model
    connect(m_transportDock, &TransportDock::bpmChanged, this, [this](int bpm) { m_model->setTempo(bpm); });
    connect(m_model, &ProjectModel::tempoChanged, this, [this](double bpm) { m_transportDock->setBPM(qRound(bpm)); });

    connect(m_audioEngine, &AudioEngine::audioError, this, [this](AudioError, const QString& message) {
        m_transportDock->setPlayingState(m_audioEngine->isPlaying());
        const QString text = message == "No audio clips loaded"
            ? QString("Nothing to play yet. Import audio (Ctrl+O) or double-click a lane to write notes.")
            : message;
        statusBar()->showMessage(text, 5000);
    });

    // Space toggles play / pause like every DAW
    QShortcut* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, m_transportDock, &TransportDock::togglePlay);

    setupMenuBar();

    setWindowTitle("Untitled"); // Shown as "Untitled - Bina" (the application display name)
    statusBar()->setSizeGripEnabled(false);
    resize(1200, 800);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onNewProjectRequested()
{
    m_audioEngine->stop();
    m_model->clearClips();
    m_model->setTempo(120);
    for (int i = 0; i < m_model->trackCount(); ++i) {
        m_model->setTrackName(i, QString("Track %1").arg(i + 1));
        m_model->setTrackColor(i, defaultTrackColor(i));
        m_model->setTrackEffects(i, {});
        m_model->setTrackMuted(i, false);
        m_model->setTrackSoloed(i, false);
        m_model->setTrackVolume(i, 1.0f);
        m_model->setTrackPan(i, 0.0f);
        m_model->setTrackInstrument(i, InstrumentSettings()); // Ignored on audio tracks
    }
    statusBar()->showMessage("New project", 3000);
}

void MainWindow::onAudioTrackRequested()
{
    const int count = m_model->trackCount();
    const int index = m_model->addTrack(QString("Track %1").arg(count + 1), defaultTrackColor(count));
    statusBar()->showMessage(QString("Added %1. Import audio onto it, or double-click its lane for a note clip")
                                 .arg(m_model->track(index).name), 5000);
}

void MainWindow::openPianoRoll(int clipId)
{
    const ClipData* clip = m_model->clip(clipId);
    if (!clip || !clip->isNoteClip()) {
        return;
    }
    // One window per clip; asking again brings it forward
    if (PianoRollWindow* existing = m_pianoRolls.value(clipId)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* window = new PianoRollWindow(m_model, clipId, this);
    connect(window, &PianoRollWindow::previewRequested, this, [this](int track, int pitch, int velocity) {
        m_audioEngine->previewNote(track, pitch, velocity);
    });
    connect(window, &PianoRollWindow::seekRequested, this, &MainWindow::seekTo);
    m_pianoRolls.insert(clipId, window);
    window->setSongPosition(m_audioEngine->currentPosition());
    window->show();
    Theme::animate(window, 0.0, 1.0, 160, [window](qreal v) { window->setWindowOpacity(v); });
}

void MainWindow::seekTo(double seconds)
{
    m_audioEngine->setTimelinePosition(seconds);
    m_timelineWidget->setIndicatorPosition(seconds);
    m_transportDock->setPlaybackPosition(seconds);
    for (const QPointer<PianoRollWindow>& window : std::as_const(m_pianoRolls)) {
        if (window) {
            window->setSongPosition(seconds);
        }
    }
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *newProjectAction = new QAction("&New Project", this);
    newProjectAction->setShortcut(QKeySequence::New);
    connect(newProjectAction, &QAction::triggered, this, &MainWindow::onNewProjectRequested);
    fileMenu->addAction(newProjectAction);

    fileMenu->addSeparator();

    QAction *loadAudioAction = new QAction("&Import Audio Files...", this);
    loadAudioAction->setShortcut(QKeySequence::Open);
    connect(loadAudioAction, &QAction::triggered, this, &MainWindow::loadAudioFile);
    fileMenu->addAction(loadAudioAction);

    fileMenu->addSeparator();

    QAction *exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);
}

int MainWindow::trackOrNew(int index)
{
    // Any track takes audio; past the last one, add tracks as needed
    while (index >= m_model->trackCount()) {
        const int count = m_model->trackCount();
        m_model->addTrack(QString("Track %1").arg(count + 1), defaultTrackColor(count));
    }
    return qMax(0, index);
}

void MainWindow::loadAudioFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, "Import Audio Files", "",
                                                            AudioDecoder::fileDialogFilter());
    if (files.isEmpty()) {
        return;
    }

    AudioImportDialog importDialog(files, m_model, this);
    if (importDialog.exec() != QDialog::Accepted) {
        return;
    }
    const AudioImportDialog::ImportSettings settings = importDialog.getImportSettings();

    QVector<ImportJob> jobs;
    for (int i = 0; i < files.size(); ++i) {
        const int track = trackOrNew(settings.oneTrackPerFile ? settings.targetTrack + i : settings.targetTrack);
        jobs.append({files[i], track, -1.0}); // Append after whatever is already on the track
    }
    importFiles(jobs);
}

void MainWindow::onFilesDropped(const QStringList& filePaths, int track, double seconds)
{
    // Dropped files land at the drop point, one per track starting at the hovered lane
    QVector<ImportJob> jobs;
    QStringList rejected;
    int nextTrack = track;
    for (const QString& path : filePaths) {
        if (AudioDecoder::isSupportedFile(path)) {
            jobs.append({path, trackOrNew(nextTrack++), seconds});
        } else {
            rejected << QString("%1: not a supported audio file").arg(QFileInfo(path).fileName());
        }
    }
    if (jobs.isEmpty()) {
        statusBar()->showMessage("None of the dropped files are supported audio files", 5000);
        return;
    }
    importFiles(jobs, rejected);
}

void MainWindow::importFiles(const QVector<ImportJob>& jobs, const QStringList& rejected)
{
    if (jobs.isEmpty()) {
        return;
    }
    // Shared by every job of this batch. Decodes finish in any order; clips are committed
    // strictly in job order so "one after another" follows the order the user chose.
    struct Batch {
        QVector<ImportJob> jobs;
        QVector<DecodeResult> results;
        QVector<bool> finished;
        int nextToCommit = 0;
        int finishedCount = 0;
        int imported = 0;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    batch->jobs = jobs;
    batch->results.resize(jobs.size());
    batch->finished.fill(false, jobs.size());
    batch->errors = rejected;

    const int total = jobs.size();
    auto showProgress = [this, batch, total]() {
        if (total == 1) {
            statusBar()->showMessage(QString("Importing %1...").arg(QFileInfo(batch->jobs.first().filePath).fileName()));
        } else {
            statusBar()->showMessage(QString("Importing %1 of %2...").arg(batch->finishedCount + 1).arg(total));
        }
    };
    showProgress();

    for (int i = 0; i < total; ++i) {
        auto* watcher = new QFutureWatcher<DecodeResult>(this);
        connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [=]() {
            batch->results[i] = watcher->result();
            batch->finished[i] = true;
            ++batch->finishedCount;
            watcher->deleteLater();

            // Commit every consecutive finished job, in order
            while (batch->nextToCommit < total && batch->finished[batch->nextToCommit]) {
                const int n = batch->nextToCommit++;
                const ImportJob& job = batch->jobs[n];
                DecodeResult& result = batch->results[n];
                if (result.ok) {
                    ClipData clip;
                    clip.track = job.track;
                    clip.start = job.start >= 0 ? job.start : m_model->trackEnd(job.track);
                    clip.filePath = job.filePath; // Color, volume and effects come from the track
                    clip.audio = result.audio;
                    clip.peaks = result.peaks;
                    m_model->addClip(clip);
                    ++batch->imported;
                } else {
                    batch->errors << QString("%1: %2").arg(QFileInfo(job.filePath).fileName(), result.error);
                }
                result = DecodeResult(); // The model holds the audio now; drop the batch's copy
            }

            if (batch->finishedCount < total) {
                showProgress();
                return;
            }

            // Batch done: one summary, and at most one error dialog
            if (total == 1 && batch->imported == 1) {
                const ImportJob& job = batch->jobs.first();
                statusBar()->showMessage(QString("Imported %1 to %2")
                    .arg(QFileInfo(job.filePath).fileName(), m_model->track(job.track).name), 5000);
            } else {
                statusBar()->showMessage(batch->errors.isEmpty()
                    ? QString("Imported %1 files").arg(batch->imported)
                    : QString("Imported %1 of %2 files").arg(batch->imported).arg(total + rejected.size()), 6000);
            }
            if (!batch->errors.isEmpty()) {
                QMessageBox::warning(this, "Import Problems",
                    QString("%1 file(s) could not be imported:\n\n%2")
                        .arg(batch->errors.size()).arg(batch->errors.join('\n')));
            }
        });
        watcher->setFuture(QtConcurrent::run(&AudioDecoder::decode, jobs[i].filePath, int(AudioEngine::SampleRate)));
    }
}

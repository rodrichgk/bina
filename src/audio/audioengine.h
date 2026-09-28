#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include <vector>

#include "audioerror.h"
#include "effects.h"
#include "instruments/instrumentapi.h"
#include "../core/projectmodel.h"

class AudioIODevice;

// Plays whatever the ProjectModel contains.
//
// Signal flow: each track mixes its audio clips and its instrument (playing the
// track's note clips) into its own bus, runs that track's effect chain on the bus,
// applies the track's volume and pan, then all buses sum into the output.
// Track processing never touches another track's audio.
//
// Threading: the audio device pulls samples via readMixedData(), which may run on
// Qt's audio thread. It never touches the model or shares a lock with the UI;
// it reads an immutable render list that is rebuilt on the UI thread whenever
// the model changes and swapped in under a pointer-sized critical section.
class AudioEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int SampleRate = 44100;
    static constexpr int Channels = 2;

    explicit AudioEngine(ProjectModel* model, QObject* parent = nullptr);
    ~AudioEngine() override;

    // Called by AudioIODevice (audio thread)
    qint64 readMixedData(char* data, qint64 maxlen);

    void play();
    void stop();
    void pause();

    bool isPlaying() const { return m_playing.load(); }
    bool isPaused() const { return m_paused; }
    double currentPosition() const { return m_position; }

    void setTimelinePosition(double seconds);

    // Plays one note on a track's instrument right away (piano roll audition).
    // Goes through the track's instrument, volume and pan, not its effects.
    void previewNote(int track, int pitch, int velocity = 100, double seconds = 0.35);

    // Master output
    void setVolume(float volume);
    float volume() const { return m_volume; }
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

signals:
    void positionChanged(double seconds);
    void playbackStateChanged(bool isPlaying);
    void audioError(AudioError error, const QString& message);

public slots:
    void onTransportPlay();
    void onTransportStop();
    void onTransportPause();
    void onTransportStopAndReturn();
    void onPositionChanged(double seconds);

private slots:
    void updatePosition();
    void rebuildRenderList();

private:
    struct RenderClip {
        std::shared_ptr<const AudioBuffer> audio;
        qint64 startFrame;
    };
    struct RenderTrack {
        std::vector<RenderClip> clips;
        std::shared_ptr<Instrument> instrument; // instrument tracks only
        std::vector<RenderNote> notes;          // every note of the track's clips, on the frame timeline
        std::shared_ptr<EffectChain> effects; // persistent across rebuilds, see m_effectChains
        float gainLeft;
        float gainRight;
    };
    using RenderList = std::vector<RenderTrack>;

    // Reuses a track's chain while its effect list is unchanged, so tweaking
    // volume doesn't reset effect state (e.g. cut a delay tail). UI thread only.
    std::shared_ptr<EffectChain> effectChainFor(int track, const QStringList& effects);
    struct CachedChain {
        QStringList names;
        std::shared_ptr<EffectChain> chain;
    };
    QHash<int, CachedChain> m_effectChains;

    // Same idea for instruments, keyed on the track's instrument revision.
    // Previews use their own instances so they never disturb playback filter state.
    struct CachedInstrument {
        int revision = -1;
        std::shared_ptr<Instrument> instrument;
    };
    std::shared_ptr<Instrument> instrumentFor(QHash<int, CachedInstrument>& cache, int track);
    QHash<int, CachedInstrument> m_instruments;
    QHash<int, CachedInstrument> m_previewInstruments;

    struct PreviewTrack {
        int track;
        std::shared_ptr<Instrument> instrument;
        std::vector<RenderNote> notes; // on the preview clock
        float gainLeft;
        float gainRight;
    };
    using PreviewList = std::vector<PreviewTrack>;
    std::shared_ptr<const PreviewList> m_previewList;
    std::atomic<qint64> m_previewFrame{0}; // advances on every pull, playing or not
    void ensureOutputRunning();

    void setupAudioOutput();
    void anchorClock();
    void applyMasterVolume();

    ProjectModel* m_model;

    QAudioFormat m_format;
    QAudioSink* m_sink = nullptr;
    AudioIODevice* m_device = nullptr;

    // Render list shared with the audio thread (swap only, never mutated in place)
    mutable QMutex m_renderMutex;
    std::shared_ptr<const RenderList> m_renderList;
    std::vector<float> m_mixBuffer; // audio thread only
    std::vector<float> m_busBuffer; // audio thread only

    // Transport
    std::atomic<bool> m_playing{false};
    std::atomic<qint64> m_framePosition{0}; // next frame the mixer will produce
    bool m_paused = false;
    double m_position = 0.0;       // seconds, UI thread
    double m_anchorPosition = 0.0; // position when the clock was anchored
    qint64 m_anchorClockMs = 0;
    QElapsedTimer m_clock;
    QTimer* m_positionTimer;

    float m_volume = 1.0f;
    bool m_muted = false;

    static constexpr int PositionUpdateIntervalMs = 16;
};

#endif // AUDIOENGINE_H

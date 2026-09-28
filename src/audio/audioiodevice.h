#ifndef AUDIOIODEVICE_H
#define AUDIOIODEVICE_H

#include <QIODevice>

class AudioEngine;

// Custom QIODevice that provides hardware-driven audio streaming
class AudioIODevice : public QIODevice
{
    Q_OBJECT

public:
    explicit AudioIODevice(AudioEngine* audioEngine, QObject* parent = nullptr);
    
    qint64 bytesAvailable() const override;

protected:
    // QIODevice interface - called by audio hardware when it needs data
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;
    bool isSequential() const override { return true; }

private:
    AudioEngine* m_audioEngine;
};

#endif // AUDIOIODEVICE_H

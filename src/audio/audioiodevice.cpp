#include "audioiodevice.h"
#include "audioengine.h"
#include <QDebug>

AudioIODevice::AudioIODevice(AudioEngine* audioEngine, QObject* parent)
    : QIODevice(parent)
    , m_audioEngine(audioEngine)
{
    // Open in read-only mode for audio output
    open(QIODevice::ReadOnly);
    qDebug() << "AudioIODevice: Opened in ReadOnly mode, isOpen():" << isOpen();
}

qint64 AudioIODevice::readData(char* data, qint64 maxlen)
{
    if (!m_audioEngine) return 0;
    
    qint64 bytesRead = m_audioEngine->readMixedData(data, maxlen);
    
    // If playback stopped or finished, we could theoretically signal completion here
    return bytesRead;
}

qint64 AudioIODevice::writeData(const char* data, qint64 len)
{
    // Not used for audio output
    Q_UNUSED(data)
    Q_UNUSED(len)
    return -1;
}

qint64 AudioIODevice::bytesAvailable() const
{
    // The mixer can generate data continuously up to the engine's duration.
    // Return a large number so QAudioSink keeps requesting data.
    return 8192000;
}

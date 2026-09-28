#ifndef AUDIOERROR_H
#define AUDIOERROR_H

#include <QString>

enum class AudioError {
    None,
    FileNotFound,
    UnsupportedFormat,
    DecodingFailed,
    DeviceError,
    MemoryError,
    InvalidParameters
};

class AudioResult {
public:
    AudioResult() : m_error(AudioError::None) {}
    AudioResult(AudioError error, const QString& message = QString()) 
        : m_error(error), m_errorMessage(message) {}
    
    bool isSuccess() const { return m_error == AudioError::None; }
    bool hasError() const { return m_error != AudioError::None; }
    
    AudioError getError() const { return m_error; }
    QString getErrorMessage() const { return m_errorMessage; }
    
    static AudioResult success() { return AudioResult(); }
    static AudioResult error(AudioError err, const QString& msg = QString()) {
        return AudioResult(err, msg);
    }
    
    QString toString() const {
        switch (m_error) {
            case AudioError::None: return "Success";
            case AudioError::FileNotFound: return "File not found";
            case AudioError::UnsupportedFormat: return "Unsupported audio format";
            case AudioError::DecodingFailed: return "Audio decoding failed";
            case AudioError::DeviceError: return "Audio device error";
            case AudioError::MemoryError: return "Memory allocation error";
            case AudioError::InvalidParameters: return "Invalid parameters";
            default: return "Unknown error";
        }
    }

private:
    AudioError m_error;
    QString m_errorMessage;
};

#endif // AUDIOERROR_H

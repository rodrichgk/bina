#ifndef EFFECTS_H
#define EFFECTS_H

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// Insert effects. Each track owns its own EffectChain, and the engine runs it on
// that track's bus only, so an effect never touches audio from other tracks.
//
// Buffers are interleaved stereo floats in 16-bit scale (+-32768).
// Effects keep state (filter memory, delay lines), so an instance must only be
// processed from one thread at a time: the audio thread.
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;
    virtual void process(float* buffer, qint64 frames) = 0;
};

class EffectChain
{
public:
    explicit EffectChain(std::vector<std::unique_ptr<AudioEffect>> effects)
        : m_effects(std::move(effects)) {}

    bool isEmpty() const { return m_effects.empty(); }
    void process(float* buffer, qint64 frames)
    {
        for (auto& effect : m_effects) {
            effect->process(buffer, frames);
        }
    }

private:
    std::vector<std::unique_ptr<AudioEffect>> m_effects;
};

namespace Effects {

// Names offered in the UI and stored in TrackData::effects
QStringList available();

// Unknown names are skipped
std::shared_ptr<EffectChain> createChain(const QStringList& names, int sampleRate);

} // namespace Effects

#endif // EFFECTS_H

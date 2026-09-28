#ifndef MUSICALGRID_H
#define MUSICALGRID_H

#include "../core/projectmodel.h"

#include <cmath>

// The timeline's grid is musical (bars and beats) and adapts to the zoom: the
// step is the finest subdivision that still leaves room between lines. The same
// step drives the drawn grid, the ruler ticks and clip snapping, so what you see
// is what you snap to.
namespace MusicalGrid {

constexpr double MinLineSpacing = 14.0; // px between grid lines

inline double stepBeats(double pixelsPerBeat)
{
    static const double steps[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    for (double step : steps) {
        if (step * pixelsPerBeat >= MinLineSpacing) {
            return step;
        }
    }
    return 128.0;
}

inline double snapSeconds(double seconds, double stepBeats, double secondsPerBeat)
{
    const double step = stepBeats * secondsPerBeat;
    return step > 0 ? std::round(seconds / step) * step : seconds;
}

// "BAR.BEAT.SIXTEENTH", 1-based, e.g. 3.2.1
inline QString position(double seconds, double secondsPerBeat)
{
    const double beats = qMax(0.0, seconds / secondsPerBeat) + 1e-9;
    const int bar = int(beats / ProjectModel::BeatsPerBar) + 1;
    const int beat = int(std::fmod(beats, ProjectModel::BeatsPerBar)) + 1;
    const int sixteenth = int((beats - std::floor(beats)) * 4) + 1;
    return QString("%1.%2.%3").arg(bar).arg(beat).arg(sixteenth);
}

} // namespace MusicalGrid

#endif // MUSICALGRID_H

#ifndef TIMESCALE_H
#define TIMESCALE_H

#include <QtGlobal>

// How seconds map to horizontal pixels on the timeline: the zoom level.
// The timeline owns one; clips and the ruler read it. Zooming changes it and
// re-lays everything out (nothing is stretched), so text and waveforms stay crisp.
class TimeScale
{
public:
    static constexpr double MinPixelsPerSecond = 6.0;
    static constexpr double MaxPixelsPerSecond = 2400.0;
    static constexpr double DefaultPixelsPerSecond = 96.0; // one beat = 48 px at 120 BPM

    double pixelsPerSecond() const { return m_pixelsPerSecond; }
    void setPixelsPerSecond(double pps) { m_pixelsPerSecond = qBound(MinPixelsPerSecond, pps, MaxPixelsPerSecond); }

    double toX(double seconds) const { return seconds * m_pixelsPerSecond; }
    double toSeconds(double x) const { return x / m_pixelsPerSecond; }

private:
    double m_pixelsPerSecond = DefaultPixelsPerSecond;
};

#endif // TIMESCALE_H

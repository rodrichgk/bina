#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QEasingCurve>
#include <functional>
#include <QFont>
#include <QIcon>
#include <QList>
#include <QPainterPath>
#include <QString>

class QApplication;
class QObject;
class QVariantAnimation;

// Single source of truth for Bina's look (the music studio of the Butu family).
// Butu's night base, one warm accent (Moto, "fire" in Lingala) answering Butu's cyan,
// red reserved for record (semantic, never decorative).
//
// Radius rule: controls and clips 4px, panels / menus / dock 6px, play button is a circle.
namespace Theme {

// Surfaces, darkest to lightest
inline const QColor Base       {0x0c, 0x0e, 0x13}; // "Butu" night: window, timeline canvas
inline const QColor Surface    {0x13, 0x15, 0x1c}; // track headers, ruler, dock
inline const QColor Raised     {0x1c, 0x1f, 0x28}; // buttons, inputs
inline const QColor Hover      {0x26, 0x2a, 0x35};
inline const QColor Border     {0x2e, 0x32, 0x40};
inline const QColor Divider    {0x1b, 0x1e, 0x26}; // track separators, minor grid

// Text ("Mwinda", light)
inline const QColor Text       {0xee, 0xf0, 0xf4};
inline const QColor TextDim    {0x9b, 0xa1, 0xad};
inline const QColor TextFaint  {0x6b, 0x71, 0x80};

// Accent + semantic
inline const QColor Accent     {0xff, 0xb4, 0x54}; // "Moto", fire
inline const QColor AccentText {0x1a, 0x12, 0x06}; // text on filled accent
inline const QColor Record     {0xff, 0x5a, 0x5f};
inline const QColor ButuCyan   {0x99, 0xf7, 0xff}; // the sibling app's accent, for family links only

// Grid
inline const QColor GridMajor  {0x22, 0x26, 0x30};
inline const QColor GridMinor  {0x16, 0x19, 0x20};

// Default clip colors, toned to sit on the dark canvas
QList<QColor> clipPalette();

QFont uiFont(qreal pointSize = 9, QFont::Weight weight = QFont::Normal);
QFont monoFont(qreal pointSize = 10, QFont::Weight weight = QFont::Medium);
// The wordmark face: Plus Jakarta Sans ExtraBold, shared with Butu
QFont brandFont(qreal pointSize);

// The Bina mark (Butu's stem and bowl with a waveform cut out of the bowl), fitted into `box`.
// `bars`: 5 for normal sizes, 3 for small, 0 for a plain b.
QPainterPath markPath(const QRectF& box, int bars = 5);
QIcon appIcon();

enum class Glyph { Play, Pause, Stop, Record, SkipBack, SkipForward, Plus, Speaker, SpeakerMuted };
// Simple geometric transport glyphs, tinted to the theme
QIcon icon(Glyph glyph, const QColor& color, int size = 16);

QString rgba(const QColor& c, int alpha);

// Blends `color` into `base`; amount 0 = base, 1 = color. Used to tint surfaces with track colors.
QColor mix(const QColor& color, const QColor& base, qreal amount);

// Motion. Every animation must say something (state change, feedback, where a thing went)
// and stay short. Follows Windows' "Animation effects" accessibility setting.
bool motionEnabled();

// Runs `apply` from `from` to `to` over `ms`, then `done`. Owned by `owner` (stops if it dies).
// With motion off it applies the end value and calls `done` immediately; returns nullptr then.
QVariantAnimation* animate(QObject* owner, qreal from, qreal to, int ms,
                           std::function<void(qreal)> apply,
                           std::function<void()> done = {},
                           const QEasingCurve& easing = QEasingCurve::OutCubic);

// Applies Fusion + dark palette + the global stylesheet
void apply(QApplication& app);

} // namespace Theme

#endif // THEME_H

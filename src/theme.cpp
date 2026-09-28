#include "theme.h"
#include "widgets/motion.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QTransform>
#include <QStyleFactory>
#include <QVariantAnimation>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Theme {

QList<QColor> clipPalette()
{
    return {
        QColor(0xd9, 0x6c, 0x5f), // brick
        QColor(0xd9, 0x93, 0x4a), // orange
        QColor(0xc9, 0xb2, 0x4e), // mustard
        QColor(0x86, 0xb0, 0x5a), // moss
        QColor(0x4f, 0xae, 0x93), // teal
        QColor(0x4f, 0x9c, 0xc4), // steel blue
        QColor(0x6f, 0x86, 0xd6), // cornflower
        QColor(0xa7, 0x7b, 0xc9), // lavender
        QColor(0xc9, 0x72, 0x9e), // rose
        QColor(0x9a, 0x9a, 0xa4), // neutral
    };
}

// Bundled (src/resources/fonts, OFL): Inter for the interface, JetBrains Mono for time readouts,
// Plus Jakarta Sans for the wordmark
static void loadBundledFonts()
{
    for (const char* file : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold",
                             "JetBrainsMono-Regular", "JetBrainsMono-Medium",
                             "PlusJakartaSans-ExtraBold"}) {
        QFontDatabase::addApplicationFont(QString(":/fonts/%1.ttf").arg(file));
    }
}

QFont uiFont(qreal pointSize, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({"Inter", "Segoe UI", "Helvetica Neue", "Arial"});
    f.setPointSizeF(pointSize);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont monoFont(qreal pointSize, QFont::Weight weight)
{
    QFont f;
    f.setFamilies({"JetBrains Mono", "Cascadia Mono", "Consolas", "Menlo", "monospace"});
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(pointSize);
    f.setWeight(weight);
    return f;
}

QFont brandFont(qreal pointSize)
{
    QFont f;
    f.setFamilies({"Plus Jakarta Sans", "Segoe UI"});
    f.setPointSizeF(pointSize);
    f.setWeight(QFont::ExtraBold);
    f.setLetterSpacing(QFont::PercentageSpacing, 96);
    return f;
}

QPainterPath markPath(const QRectF& box, int bars)
{
    // Logo units (same geometry as the brand board): the mark spans x 24..100, y 12..106
    QPainterPath shape;
    shape.addRoundedRect(QRectF(24, 12, 21, 94), 10.5, 10.5);
    QPainterPath bowl;
    bowl.addEllipse(QPointF(66, 70), 34, 34);
    shape = shape.united(bowl);

    QPainterPath cut;
    if (bars >= 5) {
        const qreal full[5][4] = {{43, 63, 6, 14}, {53, 55, 6, 30}, {63, 48, 6, 44}, {73, 56, 6, 28}, {83, 62, 6, 16}};
        for (const auto& b : full) {
            cut.addRoundedRect(QRectF(b[0], b[1], b[2], b[3]), b[2] / 2, b[2] / 2);
        }
    } else if (bars >= 3) {
        const qreal small[3][4] = {{50, 58, 9, 24}, {63, 50, 9, 40}, {76, 59, 9, 22}};
        for (const auto& b : small) {
            cut.addRoundedRect(QRectF(b[0], b[1], b[2], b[3]), b[2] / 2, b[2] / 2);
        }
    }
    if (!cut.isEmpty()) {
        shape = shape.subtracted(cut);
    }

    // Fit the 76 x 94 mark into the box, centered
    const QRectF bounds(24, 12, 76, 94);
    const qreal scale = qMin(box.width() / bounds.width(), box.height() / bounds.height());
    QTransform t;
    t.translate(box.center().x(), box.center().y());
    t.scale(scale, scale);
    t.translate(-bounds.center().x(), -bounds.center().y());
    return t.map(shape);
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/brand/bina-%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

QString rgba(const QColor& c, int alpha)
{
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QColor mix(const QColor& color, const QColor& base, qreal amount)
{
    return QColor::fromRgbF(color.redF() * amount + base.redF() * (1 - amount),
                            color.greenF() * amount + base.greenF() * (1 - amount),
                            color.blueF() * amount + base.blueF() * (1 - amount));
}

static QPixmap renderGlyph(Glyph glyph, const QColor& color, int size, qreal dpr)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const qreal s = size;
    const qreal m = s * 0.18; // inner margin

    switch (glyph) {
    case Glyph::Play: {
        QPainterPath path;
        path.moveTo(m + s * 0.08, m);
        path.lineTo(s - m, s / 2);
        path.lineTo(m + s * 0.08, s - m);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Glyph::Pause: {
        const qreal w = s * 0.2;
        p.drawRoundedRect(QRectF(s * 0.26, m, w, s - 2 * m), 1, 1);
        p.drawRoundedRect(QRectF(s * 0.54, m, w, s - 2 * m), 1, 1);
        break;
    }
    case Glyph::Stop:
        p.drawRoundedRect(QRectF(m + 1, m + 1, s - 2 * m - 2, s - 2 * m - 2), 1.5, 1.5);
        break;
    case Glyph::Record:
        p.drawEllipse(QRectF(m + 1, m + 1, s - 2 * m - 2, s - 2 * m - 2));
        break;
    case Glyph::SkipBack: {
        p.drawRect(QRectF(m, m, s * 0.1, s - 2 * m));
        QPainterPath path;
        path.moveTo(s - m, m);
        path.lineTo(m + s * 0.12, s / 2);
        path.lineTo(s - m, s - m);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Glyph::SkipForward: {
        QPainterPath path;
        path.moveTo(m, m);
        path.lineTo(s - m - s * 0.12, s / 2);
        path.lineTo(m, s - m);
        path.closeSubpath();
        p.drawPath(path);
        p.drawRect(QRectF(s - m - s * 0.1, m, s * 0.1, s - 2 * m));
        break;
    }
    case Glyph::Speaker:
    case Glyph::SpeakerMuted: {
        // Speaker body + cone
        QPainterPath cone;
        cone.moveTo(s * 0.12, s * 0.38);
        cone.lineTo(s * 0.28, s * 0.38);
        cone.lineTo(s * 0.50, s * 0.18);
        cone.lineTo(s * 0.50, s * 0.82);
        cone.lineTo(s * 0.28, s * 0.62);
        cone.lineTo(s * 0.12, s * 0.62);
        cone.closeSubpath();
        p.drawPath(cone);

        QPen stroke(color, s * 0.09, Qt::SolidLine, Qt::RoundCap);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        if (glyph == Glyph::Speaker) {
            // Two sound waves
            p.drawArc(QRectF(s * 0.36, s * 0.34, s * 0.30, s * 0.32), -50 * 16, 100 * 16);
            p.drawArc(QRectF(s * 0.34, s * 0.20, s * 0.50, s * 0.60), -50 * 16, 100 * 16);
        } else {
            // Cross
            p.drawLine(QPointF(s * 0.62, s * 0.37), QPointF(s * 0.88, s * 0.63));
            p.drawLine(QPointF(s * 0.88, s * 0.37), QPointF(s * 0.62, s * 0.63));
        }
        break;
    }
    case Glyph::Plus: {
        const qreal t = s * 0.1;
        p.drawRoundedRect(QRectF(s / 2 - t / 2, m, t, s - 2 * m), t / 2, t / 2);
        p.drawRoundedRect(QRectF(m, s / 2 - t / 2, s - 2 * m, t), t / 2, t / 2);
        break;
    }
    }
    return pm;
}

QIcon icon(Glyph glyph, const QColor& color, int size)
{
    QIcon ic;
    ic.addPixmap(renderGlyph(glyph, color, size, 1.0));
    ic.addPixmap(renderGlyph(glyph, color, size, 2.0));
    return ic;
}

static QString styleSheet()
{
    QString qss = R"(
QMainWindow, QDialog { background: @base; }
QWidget { color: @text; }

QToolTip {
    background: @raised; color: @text;
    border: 1px solid @border; padding: 4px 6px;
}

/* Menus */
QMenuBar { background: @surface; border-bottom: 1px solid @divider; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: @hover; }
QMenu {
    background: @surface; border: 1px solid @border;
    padding: 4px; border-radius: 6px;
}
QMenu::item { padding: 6px 24px 6px 10px; border-radius: 4px; }
QMenu::item:selected { background: @hover; color: @text; }
QMenu::icon { padding-left: 8px; }
QMenu::separator { height: 1px; background: @divider; margin: 4px 6px; }

QStatusBar { background: @surface; color: @textDim; border-top: 1px solid @divider; }
QStatusBar::item { border: none; }

/* Buttons */
QPushButton {
    background: @raised; color: @text;
    border: 1px solid @border; border-radius: 4px;
    padding: 6px 14px;
}
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @surface; }
QPushButton:checked { background: @accentSoft; border-color: @accent; color: @accent; }
QPushButton:disabled { color: @textFaint; background: @surface; }
QPushButton:focus { border-color: @accentLine; }
QPushButton[primary="true"] {
    background: @accent; color: @accentText; border: 1px solid @accent; font-weight: 600;
}
QPushButton[primary="true"]:hover { background: @accentHover; }
QPushButton[primary="true"]:pressed { background: @accentPressed; }

/* Inputs */
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: @raised; border: 1px solid @border; border-radius: 4px;
    padding: 5px 8px;
    selection-background-color: @accent; selection-color: @accentText;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: #3a3f4f; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border-color: @accent; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: @surface; border: 1px solid @border; outline: 0;
    selection-background-color: @hover; selection-color: @text;
}

QGroupBox {
    border: 1px solid @divider; border-radius: 6px;
    margin-top: 18px; padding: 12px 10px 10px 10px;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 2px; padding: 0 2px;
    color: @textDim; font-weight: 600;
}

QLabel { background: transparent; }

QListWidget {
    background: @surface; border: 1px solid @divider; border-radius: 4px; outline: 0;
}
QListWidget::item { padding: 4px 6px; }
QListWidget::item:selected { background: @hover; color: @text; }

/* Sliders */
QSlider::groove:horizontal { height: 4px; background: @raised; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal {
    background: @text; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::groove:vertical { width: 4px; background: @raised; border-radius: 2px; }
QSlider::add-page:vertical { background: @accent; border-radius: 2px; }
QSlider::handle:vertical {
    background: @text; width: 12px; height: 12px; margin: 0 -4px; border-radius: 6px;
}

/* Scrollbars: thin, unobtrusive */
QScrollBar:horizontal { background: @base; height: 10px; margin: 0; }
QScrollBar:vertical { background: @base; width: 10px; margin: 0; }
QScrollBar::handle { background: @hover; border-radius: 3px; margin: 2px; }
QScrollBar::handle:hover { background: #3a3f4f; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QAbstractScrollArea::corner { background: @base; }

QSplitter::handle { background: @divider; }
QSplitter::handle:horizontal { width: 1px; }
)";

    const QColor accentHover = Accent.lighter(110);
    const QColor accentPressed = Accent.darker(115);

    // Longest keys first so "@accentText" isn't eaten by "@accent"
    const QList<QPair<QString, QString>> tokens = {
        {"@accentPressed", accentPressed.name()},
        {"@accentHover", accentHover.name()},
        {"@accentText", AccentText.name()},
        {"@accentSoft", rgba(Accent, 36)},
        {"@accentLine", rgba(Accent, 140)},
        {"@accent", Accent.name()},
        {"@textFaint", TextFaint.name()},
        {"@textDim", TextDim.name()},
        {"@text", Text.name()},
        {"@surface", Surface.name()},
        {"@raised", Raised.name()},
        {"@hover", Hover.name()},
        {"@border", Border.name()},
        {"@divider", Divider.name()},
        {"@base", Base.name()},
    };
    for (const auto& t : tokens)
        qss.replace(t.first, t.second);
    return qss;
}

bool motionEnabled()
{
    // MUSICAPP_NO_MOTION=1 turns every animation off (testing, screen recording)
    if (qEnvironmentVariableIsSet("MUSICAPP_NO_MOTION")) {
        return false;
    }
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return enabled != FALSE;
    }
#endif
    return true;
}

QVariantAnimation* animate(QObject* owner, qreal from, qreal to, int ms,
                           std::function<void(qreal)> apply,
                           std::function<void()> done,
                           const QEasingCurve& easing)
{
    if (!motionEnabled() || ms <= 0) {
        apply(to);
        if (done) {
            done();
        }
        return nullptr;
    }
    auto* animation = new QVariantAnimation(owner);
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setDuration(ms);
    animation->setEasingCurve(easing);
    QObject::connect(animation, &QVariantAnimation::valueChanged, owner,
                     [apply](const QVariant& value) { apply(value.toReal()); });
    if (done) {
        QObject::connect(animation, &QVariantAnimation::finished, owner, done);
    }
    apply(from);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    return animation;
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    loadBundledFonts();
    app.setFont(uiFont());

    QPalette pal;
    pal.setColor(QPalette::Window, Base);
    pal.setColor(QPalette::WindowText, Text);
    pal.setColor(QPalette::Base, Raised);
    pal.setColor(QPalette::AlternateBase, Surface);
    pal.setColor(QPalette::Text, Text);
    pal.setColor(QPalette::PlaceholderText, TextFaint);
    pal.setColor(QPalette::Button, Raised);
    pal.setColor(QPalette::ButtonText, Text);
    pal.setColor(QPalette::BrightText, Record);
    pal.setColor(QPalette::Highlight, Accent);
    pal.setColor(QPalette::HighlightedText, AccentText);
    pal.setColor(QPalette::ToolTipBase, Raised);
    pal.setColor(QPalette::ToolTipText, Text);
    pal.setColor(QPalette::Link, Accent);
    pal.setColor(QPalette::Light, Hover);
    pal.setColor(QPalette::Midlight, Border);
    pal.setColor(QPalette::Mid, Border);
    pal.setColor(QPalette::Dark, Surface);
    pal.setColor(QPalette::Shadow, Base);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, TextFaint);
    app.setPalette(pal);

    app.setStyleSheet(styleSheet());
    Motion::install(app);
}

} // namespace Theme

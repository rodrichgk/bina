#include "pianoroll.h"
#include "../theme.h"

#include <QVariantAnimation>

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

using namespace PianoRoll;

namespace {

constexpr int PitchCount = HighestPitch - LowestPitch + 1;
constexpr double MinNoteLength = 1.0 / 32.0; // beats, when snapping is off

// Keyboard key colors (only used here, so kept local rather than in the theme)
const QColor WhiteKey(0xcf, 0xd0, 0xd6);
const QColor WhiteKeyEdge(0x9a, 0x9b, 0xa4);
const QColor BlackKey(0x1c, 0x1c, 0x21);

} // namespace

QString PianoRoll::pitchName(int pitch)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return QString("%1%2").arg(names[pitch % 12]).arg(pitch / 12 - 1);
}

bool PianoRoll::isBlackKey(int pitch)
{
    switch (pitch % 12) {
    case 1: case 3: case 6: case 8: case 10:
        return true;
    default:
        return false;
    }
}

// ===========================================================================
// Canvas

PianoRollCanvas::PianoRollCanvas(ProjectModel* model, int clipId, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_clipId(clipId)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    connect(m_model, &ProjectModel::clipChanged, this, [this](int id) {
        if (id == m_clipId && m_drag == Drag::None) {
            reloadFromModel();
        }
    });
    connect(m_model, &ProjectModel::trackChanged, this, [this]() {
        if (const ClipData* clip = m_model->clip(m_clipId)) {
            m_color = m_model->track(clip->track).color;
            update();
        }
    });
    reloadFromModel();
}

void PianoRollCanvas::reloadFromModel()
{
    const ClipData* clip = m_model->clip(m_clipId);
    if (!clip) {
        return;
    }
    m_notes = clip->notes;
    m_lengthBeats = clip->lengthBeats;
    m_color = m_model->track(clip->track).color;
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        it = *it >= m_notes.size() ? m_selected.erase(it) : std::next(it);
    }
    updateSize();
    update();
}

void PianoRollCanvas::updateSize()
{
    // Room for four more bars after the clip so notes can be drawn past the end
    double lastEnd = m_lengthBeats;
    for (const Note& n : std::as_const(m_notes)) {
        lastEnd = qMax(lastEnd, n.end());
    }
    const double beats = std::ceil(lastEnd / ProjectModel::BeatsPerBar) * ProjectModel::BeatsPerBar
                         + 4 * ProjectModel::BeatsPerBar;
    setFixedSize(int(beats * m_pxPerBeat), PitchCount * KeyHeight);
    emit geometryChanged();
}

QSize PianoRollCanvas::sizeHint() const
{
    return size();
}

void PianoRollCanvas::setPlayheadBeat(double beat)
{
    if (!qFuzzyCompare(beat + 10.0, m_playheadBeat + 10.0)) {
        m_playheadBeat = beat;
        update();
    }
}

int PianoRollCanvas::pitchAt(qreal y) const
{
    return qBound(LowestPitch, HighestPitch - int(std::floor(y / KeyHeight)), HighestPitch);
}

qreal PianoRollCanvas::yOf(int pitch) const
{
    return (HighestPitch - pitch) * KeyHeight;
}

double PianoRollCanvas::snapDown(double beat) const
{
    return m_snap > 0 ? std::floor(beat / m_snap + 1e-9) * m_snap : beat;
}

double PianoRollCanvas::snapNearest(double beat) const
{
    return m_snap > 0 ? std::round(beat / m_snap) * m_snap : beat;
}

QRectF PianoRollCanvas::noteRect(const Note& note) const
{
    return QRectF(note.start * m_pxPerBeat, yOf(note.pitch), note.length * m_pxPerBeat, KeyHeight);
}

int PianoRollCanvas::noteAt(const QPointF& pos) const
{
    // Last drawn is on top
    for (int i = m_notes.size() - 1; i >= 0; --i) {
        if (noteRect(m_notes[i]).contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool PianoRollCanvas::onResizeEdge(const QPointF& pos, int index) const
{
    const QRectF r = noteRect(m_notes[index]);
    return pos.x() >= r.right() - qMin(6.0, r.width() / 3);
}

void PianoRollCanvas::popNote(int index)
{
    m_popIndex = index;
    Theme::animate(this, 0.0, 1.0, 110, [this](qreal v) {
        m_popProgress = v;
        update();
    }, {}, QEasingCurve::OutBack);
}

void PianoRollCanvas::leaveGhost(const QRectF& rect)
{
    m_ghosts.append(rect);
    if (m_ghostAnimation) {
        m_ghostAnimation->stop(); // Restart the fade for the whole batch (stop() doesn't clear)
    }
    m_ghostAnimation = Theme::animate(this, 1.0, 0.0, 160, [this](qreal v) {
        m_ghostOpacity = v;
        update();
    }, [this]() {
        m_ghosts.clear();
        update();
    });
}

void PianoRollCanvas::eraseAt(const QPointF& pos)
{
    const int index = noteAt(pos);
    if (index < 0) {
        return;
    }
    leaveGhost(noteRect(m_notes[index]));
    if (m_popIndex == index) {
        m_popIndex = -1;
    }
    m_notes.removeAt(index);
    // Keep selection indices pointing at the same notes
    QSet<int> shifted;
    for (int s : std::as_const(m_selected)) {
        if (s != index) {
            shifted.insert(s > index ? s - 1 : s);
        }
    }
    m_selected = shifted;
    update();
}

void PianoRollCanvas::commit()
{
    m_model->setClipNotes(m_clipId, m_notes); // Echoes back through reloadFromModel()
    updateSize();
}

// ---------------------------------------------------------------------------
// Painting

void PianoRollCanvas::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    const QRect dirty = event->rect();

    // Rows: black-key rows a step darker, an octave line under every C
    const int firstPitch = pitchAt(dirty.bottom());
    const int lastPitch = pitchAt(dirty.top());
    const QColor octaveLine = Theme::Border;
    for (int pitch = firstPitch; pitch <= lastPitch; ++pitch) {
        const QRectF row(dirty.left(), yOf(pitch), dirty.width(), KeyHeight);
        p.fillRect(row, isBlackKey(pitch) ? Theme::Base : Theme::Surface);
        if (pitch % 12 == 0) {
            p.setPen(octaveLine);
            p.drawLine(QPointF(row.left(), row.bottom()), QPointF(row.right(), row.bottom()));
        }
    }

    // Grid: snap steps, beats, bars (each only when there's room to see it)
    const double firstBeat = std::floor(beatAt(dirty.left()));
    const double lastBeat = beatAt(dirty.right()) + 1;
    auto drawLines = [&](double step, const QColor& color) {
        if (step <= 0 || step * m_pxPerBeat < 6) {
            return;
        }
        p.setPen(color);
        for (double b = std::floor(firstBeat / step) * step; b <= lastBeat; b += step) {
            const qreal x = std::round(b * m_pxPerBeat) + 0.5;
            p.drawLine(QPointF(x, dirty.top()), QPointF(x, dirty.bottom() + 1));
        }
    };
    if (m_snap > 0 && m_snap < 1) {
        drawLines(m_snap, Theme::GridMinor);
    }
    drawLines(1.0, Theme::GridMajor);
    drawLines(ProjectModel::BeatsPerBar, Theme::Border.lighter(125));

    // Past the end of the clip: dimmed, notes there won't play until the clip grows
    const qreal endX = m_lengthBeats * m_pxPerBeat;
    if (endX < dirty.right()) {
        QColor shade = Theme::Base;
        shade.setAlpha(150);
        p.fillRect(QRectF(qMax<qreal>(endX, dirty.left()), dirty.top(), dirty.right() - endX + 1, dirty.height()), shade);
        p.setPen(QPen(Theme::TextFaint, 1));
        p.drawLine(QPointF(endX + 0.5, dirty.top()), QPointF(endX + 0.5, dirty.bottom() + 1));
    }

    // Notes, brighter with velocity; selected ones get the accent outline
    p.setRenderHint(QPainter::Antialiasing);
    const QFont labelFont = Theme::uiFont(7.5, QFont::Medium);
    p.setFont(labelFont);
    // Ghosts of just-deleted notes
    if (!m_ghosts.isEmpty() && m_ghostOpacity > 0) {
        QColor ghost = m_color;
        ghost.setAlphaF(0.6 * m_ghostOpacity);
        p.setPen(Qt::NoPen);
        p.setBrush(ghost);
        for (const QRectF& g : std::as_const(m_ghosts)) {
            p.drawRoundedRect(g.adjusted(0.5, 1.5, -0.5, -1.5), 2, 2);
        }
    }

    for (int i = 0; i < m_notes.size(); ++i) {
        QRectF r = noteRect(m_notes[i]).adjusted(0.5, 1.5, -0.5, -1.5);
        if (i == m_popIndex && m_popProgress < 1.0) {
            // Grows from its left edge, where it was placed
            const qreal s = 0.7 + 0.3 * m_popProgress;
            r = QRectF(r.left(), r.center().y() - r.height() * s / 2, r.width() * s, r.height() * s);
        }
        if (!r.intersects(dirty)) {
            continue;
        }
        const bool selected = m_selected.contains(i);
        QColor fill = selected ? m_color.lighter(125) : m_color;
        fill.setAlphaF(0.55 + 0.45 * m_notes[i].velocity / 127.0);
        p.setBrush(fill);
        p.setPen(selected ? QPen(Theme::Accent, 1.5) : QPen(m_color.darker(170), 1));
        p.drawRoundedRect(r, 2, 2);
        if (r.width() > 30) {
            p.setPen(Theme::Base);
            p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, pitchName(m_notes[i].pitch));
        }
    }

    // Rubber-band selection
    if (m_drag == Drag::Select) {
        QColor tint = Theme::Accent;
        tint.setAlpha(30);
        p.setBrush(tint);
        p.setPen(QPen(Theme::Accent, 1));
        p.drawRect(m_selectionRect);
    }

    // Song playhead
    if (m_playheadBeat >= 0) {
        const qreal x = m_playheadBeat * m_pxPerBeat;
        p.setPen(QPen(Theme::Accent, 1.25));
        p.drawLine(QPointF(x, dirty.top()), QPointF(x, dirty.bottom() + 1));
    }
}

// ---------------------------------------------------------------------------
// Mouse & keyboard

void PianoRollCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    const QPointF pos = event->position();
    int index = noteAt(pos);

    if (event->button() == Qt::RightButton) {
        // Right-click deletes; keep the button down to erase along the way
        m_drag = Drag::Erase;
        eraseAt(pos);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if (ctrl && index < 0) {
        m_drag = Drag::Select;
        m_pressPos = pos;
        m_selectionRect = QRectF(pos, pos);
        m_selected.clear();
        update();
        return;
    }
    if (ctrl && index >= 0) {
        // Ctrl-click toggles a note in the selection
        if (!m_selected.remove(index)) {
            m_selected.insert(index);
        }
        update();
        return;
    }

    if (index >= 0) {
        if (!m_selected.contains(index)) {
            m_selected = {index};
        }
        m_drag = onResizeEdge(pos, index) ? Drag::Resize : Drag::Move;
    } else {
        // Empty spot: add a note of the last used length
        Note note;
        note.pitch = pitchAt(pos.y());
        note.start = qMax(0.0, snapDown(beatAt(pos.x())));
        note.length = m_lastLength;
        m_notes.append(note);
        index = m_notes.size() - 1;
        m_selected = {index};
        m_drag = Drag::Move;
        popNote(index);
    }

    m_dragNote = index;
    m_pressPos = pos;
    m_dragOrigin = m_notes;
    m_lastPreviewPitch = m_notes[index].pitch;
    if (m_drag == Drag::Move) {
        emit previewRequested(m_notes[index].pitch, m_notes[index].velocity);
    }
    update();
}

void PianoRollCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    switch (m_drag) {
    case Drag::None: {
        const int index = noteAt(pos);
        setCursor(index < 0 ? Qt::ArrowCursor
                            : onResizeEdge(pos, index) ? Qt::SizeHorCursor : Qt::PointingHandCursor);
        return;
    }
    case Drag::Erase:
        eraseAt(pos);
        return;
    case Drag::Select: {
        m_selectionRect = QRectF(m_pressPos, pos).normalized();
        m_selected.clear();
        for (int i = 0; i < m_notes.size(); ++i) {
            if (noteRect(m_notes[i]).intersects(m_selectionRect)) {
                m_selected.insert(i);
            }
        }
        update();
        return;
    }
    case Drag::Move: {
        const Note& anchor = m_dragOrigin[m_dragNote];
        double shift = snapNearest(anchor.start + beatAt(pos.x() - m_pressPos.x())) - anchor.start;
        int transpose = pitchAt(pos.y()) - pitchAt(m_pressPos.y());
        // Keep the whole selection inside the grid
        for (int s : std::as_const(m_selected)) {
            const Note& o = m_dragOrigin[s];
            shift = qMax(shift, -o.start);
            transpose = qBound(LowestPitch - o.pitch, transpose, HighestPitch - o.pitch);
        }
        for (int s : std::as_const(m_selected)) {
            m_notes[s].start = m_dragOrigin[s].start + shift;
            m_notes[s].pitch = m_dragOrigin[s].pitch + transpose;
        }
        if (m_notes[m_dragNote].pitch != m_lastPreviewPitch) {
            m_lastPreviewPitch = m_notes[m_dragNote].pitch;
            emit previewRequested(m_lastPreviewPitch, m_notes[m_dragNote].velocity);
        }
        update();
        return;
    }
    case Drag::Resize: {
        const Note& anchor = m_dragOrigin[m_dragNote];
        const double minLength = m_snap > 0 ? m_snap : MinNoteLength;
        const double newLength = qMax(minLength, snapNearest(beatAt(pos.x())) - anchor.start);
        const double delta = newLength - anchor.length;
        for (int s : std::as_const(m_selected)) {
            m_notes[s].length = qMax(minLength, m_dragOrigin[s].length + delta);
        }
        update();
        return;
    }
    }
}

void PianoRollCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    const Drag finished = m_drag;
    m_drag = Drag::None;

    if (finished == Drag::Move || finished == Drag::Resize) {
        m_lastLength = m_notes[m_dragNote].length; // The next new note uses this length
        commit();
    } else if (finished == Drag::Erase) {
        commit();
    }
    m_dragNote = -1;
    update();
}

void PianoRollCanvas::keyPressEvent(QKeyEvent* event)
{
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        QList<int> doomed = m_selected.values();
        std::sort(doomed.begin(), doomed.end(), std::greater<int>());
        for (int i : doomed) {
            leaveGhost(noteRect(m_notes[i]));
            m_notes.removeAt(i);
        }
        m_popIndex = -1;
        m_selected.clear();
        commit();
        return;
    }
    if (ctrl && event->key() == Qt::Key_A) {
        m_selected.clear();
        for (int i = 0; i < m_notes.size(); ++i) {
            m_selected.insert(i);
        }
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        m_selected.clear();
        update();
        return;
    }

    // Arrows nudge the selection: up/down a semitone (Shift: an octave), left/right a grid step
    int transpose = 0;
    double shiftBeats = 0;
    const double step = m_snap > 0 ? m_snap : 1.0;
    switch (event->key()) {
    case Qt::Key_Up:    transpose = shift ? 12 : 1; break;
    case Qt::Key_Down:  transpose = shift ? -12 : -1; break;
    case Qt::Key_Right: shiftBeats = step; break;
    case Qt::Key_Left:  shiftBeats = -step; break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    if (m_selected.isEmpty()) {
        return;
    }
    for (int s : std::as_const(m_selected)) {
        transpose = qBound(LowestPitch - m_notes[s].pitch, transpose, HighestPitch - m_notes[s].pitch);
        shiftBeats = qMax(shiftBeats, -m_notes[s].start);
    }
    for (int s : std::as_const(m_selected)) {
        m_notes[s].pitch += transpose;
        m_notes[s].start += shiftBeats;
    }
    if (transpose != 0) {
        const Note& first = m_notes[*m_selected.begin()];
        emit previewRequested(first.pitch, first.velocity);
    }
    commit();
    update();
}

void PianoRollCanvas::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        event->ignore(); // The scroll area scrolls
        return;
    }
    // Ctrl+wheel zooms time around the cursor
    const qreal x = event->position().x();
    const double anchorBeat = beatAt(x);
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    m_pxPerBeat = qBound(24.0, m_pxPerBeat * factor, 480.0);
    updateSize();
    emit zoomChanged(m_pxPerBeat, anchorBeat, int(x));
    update();
    event->accept();
}

// ===========================================================================
// Keyboard

PianoKeyboard::PianoKeyboard(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(KeyboardWidth);
    setCursor(Qt::PointingHandCursor);
}

void PianoKeyboard::setOffset(int y)
{
    m_offset = y;
    update();
}

void PianoKeyboard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(rect(), Theme::Base);
    p.setFont(Theme::monoFont(7.5));

    const int blackWidth = int(width() * 0.62);
    for (int pitch = LowestPitch; pitch <= HighestPitch; ++pitch) {
        const int y = (HighestPitch - pitch) * KeyHeight - m_offset;
        if (y + KeyHeight < 0 || y > height()) {
            continue;
        }
        const QRect row(0, y, width() - 1, KeyHeight);
        const bool pressed = pitch == m_pressedPitch;
        if (isBlackKey(pitch)) {
            p.fillRect(row, WhiteKey);
            p.fillRect(QRect(0, y + 1, blackWidth, KeyHeight - 2), pressed ? Theme::Accent : BlackKey);
        } else {
            p.fillRect(row, pressed ? Theme::Accent : WhiteKey);
            p.setPen(WhiteKeyEdge);
            p.drawLine(row.bottomLeft(), row.bottomRight());
            if (pitch % 12 == 0) {
                p.setPen(Theme::Base);
                p.drawText(row.adjusted(0, 0, -5, 0), Qt::AlignVCenter | Qt::AlignRight, pitchName(pitch));
            }
        }
    }
    p.setPen(Theme::Border);
    p.drawLine(width() - 1, 0, width() - 1, height());
}

void PianoKeyboard::mousePressEvent(QMouseEvent* event)
{
    const int pitch = qBound(LowestPitch, HighestPitch - int((event->position().y() + m_offset) / KeyHeight), HighestPitch);
    m_pressedPitch = pitch;
    emit keyPressed(pitch);
    update();
}

void PianoKeyboard::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    m_pressedPitch = -1;
    update();
}

// ===========================================================================
// Ruler

PianoRuler::PianoRuler(PianoRollCanvas* canvas, QWidget* parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    setFixedHeight(RulerHeight);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click to move the song position here");
}

void PianoRuler::setOffset(int x)
{
    m_offset = x;
    update();
}

void PianoRuler::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(rect(), Theme::Surface);

    const double ppb = m_canvas->pixelsPerBeat();
    const QFont font = Theme::monoFont(8);
    const QFontMetrics fm(font);
    p.setFont(font);

    const int firstBeat = int(m_offset / ppb);
    const int lastBeat = int((m_offset + width()) / ppb) + 1;
    for (int beat = firstBeat; beat <= lastBeat; ++beat) {
        const qreal x = beat * ppb - m_offset + 0.5;
        const bool bar = beat % ProjectModel::BeatsPerBar == 0;
        p.setPen(Theme::TextFaint);
        p.drawLine(QPointF(x, height() - (bar ? 9 : 5)), QPointF(x, height()));
        if (bar) {
            p.setPen(Theme::TextDim);
            p.drawText(QPointF(x + 4, (height() + fm.ascent()) / 2 - 3),
                       QString::number(beat / ProjectModel::BeatsPerBar + 1));
        }
    }

    // Clip end marker
    const qreal endX = m_canvas->lengthBeats() * ppb - m_offset;
    p.setPen(QPen(Theme::Accent, 1.5));
    p.drawLine(QPointF(endX, height() - 10), QPointF(endX, height()));

    p.setPen(Theme::Border);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void PianoRuler::mousePressEvent(QMouseEvent* event)
{
    emit beatClicked(qMax(0.0, (event->position().x() + m_offset) / m_canvas->pixelsPerBeat()));
}

// ===========================================================================
// Window

PianoRollWindow::PianoRollWindow(ProjectModel* model, int clipId, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_clipId(clipId)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(true);
    resize(1080, 640);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar: which track, snap, how-to
    QWidget* toolbar = new QWidget();
    toolbar->setObjectName("pianoRollToolbar");
    toolbar->setAttribute(Qt::WA_StyledBackground, true);
    toolbar->setStyleSheet(QString("QWidget#pianoRollToolbar { background: %1; border-bottom: 1px solid %2; }")
                               .arg(Theme::Surface.name(), Theme::Border.name()));
    QHBoxLayout* bar = new QHBoxLayout(toolbar);
    bar->setContentsMargins(14, 8, 14, 8);
    bar->setSpacing(10);

    m_trackLabel = new QLabel();
    m_trackLabel->setFont(Theme::uiFont(9.5, QFont::Medium));

    m_snapCombo = new QComboBox();
    const QList<QPair<QString, double>> snaps = {
        {"Bar", 4.0}, {"Beat", 1.0}, {"1/2 beat", 0.5}, {"1/4 beat", 0.25}, {"1/8 beat", 0.125}, {"Off", 0.0}};
    for (const auto& s : snaps) {
        m_snapCombo->addItem(s.first, s.second);
    }
    m_snapCombo->setCurrentIndex(3);

    QLabel* hint = new QLabel("Click to add, drag to move or resize, right-click to delete, Ctrl+drag to select");
    hint->setStyleSheet(QString("color: %1;").arg(Theme::TextFaint.name()));

    bar->addWidget(m_trackLabel);
    bar->addSpacing(12);
    bar->addWidget(new QLabel("Snap"));
    bar->addWidget(m_snapCombo);
    bar->addStretch();
    bar->addWidget(hint);
    layout->addWidget(toolbar);

    // Keyboard | grid, with the ruler above the grid
    m_canvas = new PianoRollCanvas(model, clipId);
    m_keyboard = new PianoKeyboard();
    m_ruler = new PianoRuler(m_canvas);

    m_scroll = new QScrollArea();
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(false);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet(QString("QScrollArea { background: %1; }").arg(Theme::Base.name()));

    QWidget* corner = new QWidget();
    corner->setFixedSize(KeyboardWidth, RulerHeight);
    corner->setAttribute(Qt::WA_StyledBackground, true);
    corner->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                              .arg(Theme::Surface.name(), Theme::Border.name()));

    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(corner, 0, 0);
    grid->addWidget(m_ruler, 0, 1);
    grid->addWidget(m_keyboard, 1, 0);
    grid->addWidget(m_scroll, 1, 1);
    layout->addLayout(grid, 1);

    // Keep keyboard and ruler aligned with the scrolled grid
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, m_keyboard, &PianoKeyboard::setOffset);
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, m_ruler, &PianoRuler::setOffset);
    connect(m_canvas, &PianoRollCanvas::geometryChanged, m_ruler, qOverload<>(&QWidget::update));
    connect(m_canvas, &PianoRollCanvas::zoomChanged, this, [this](double ppb, double anchorBeat, int canvasX) {
        QScrollBar* h = m_scroll->horizontalScrollBar();
        const int viewportX = canvasX - h->value();
        h->setValue(int(anchorBeat * ppb) - viewportX); // Zoom around the cursor
        m_ruler->update();
    });

    connect(m_snapCombo, &QComboBox::currentIndexChanged, this, [this]() {
        m_canvas->setSnap(m_snapCombo->currentData().toDouble());
        m_canvas->update();
    });
    m_canvas->setSnap(m_snapCombo->currentData().toDouble());

    // Audition and seeking go out through the main window
    auto currentTrack = [this]() {
        const ClipData* clip = m_model->clip(m_clipId);
        return clip ? clip->track : -1;
    };
    connect(m_canvas, &PianoRollCanvas::previewRequested, this, [this, currentTrack](int pitch, int velocity) {
        emit previewRequested(currentTrack(), pitch, velocity);
    });
    connect(m_keyboard, &PianoKeyboard::keyPressed, this, [this, currentTrack](int pitch) {
        emit previewRequested(currentTrack(), pitch, 100);
    });
    connect(m_ruler, &PianoRuler::beatClicked, this, [this](double beat) {
        if (const ClipData* clip = m_model->clip(m_clipId)) {
            emit seekRequested(clip->start + beat * m_model->secondsPerBeat());
        }
    });

    // Follow the model
    connect(m_model, &ProjectModel::clipRemoved, this, [this](int id) {
        if (id == m_clipId) {
            close();
        }
    });
    connect(m_model, &ProjectModel::trackChanged, this, &PianoRollWindow::updateTitle);
    connect(m_model, &ProjectModel::clipChanged, this, [this](int id) {
        if (id == m_clipId) {
            updateTitle();
        }
    });
    updateTitle();

    // Open around middle C
    QTimer::singleShot(0, this, [this]() {
        const int y = (HighestPitch - 72) * KeyHeight;
        m_scroll->verticalScrollBar()->setValue(y - m_scroll->viewport()->height() / 4);
        m_canvas->setFocus();
    });
}

void PianoRollWindow::updateTitle()
{
    const ClipData* clip = m_model->clip(m_clipId);
    if (!clip) {
        return;
    }
    const TrackData& track = m_model->track(clip->track);
    setWindowTitle(QString("%1 - Piano Roll").arg(track.name));
    m_trackLabel->setText(track.name);
    m_trackLabel->setStyleSheet(QString("QLabel { border-left: 3px solid %1; padding-left: 8px; color: %2; }")
                                    .arg(track.color.name(), Theme::Text.name()));
}

void PianoRollWindow::setSongPosition(double seconds)
{
    const ClipData* clip = m_model->clip(m_clipId);
    if (!clip) {
        return;
    }
    const double beat = (seconds - clip->start) / m_model->secondsPerBeat();
    m_canvas->setPlayheadBeat(beat >= 0 && beat <= clip->lengthBeats ? beat : -1.0);
}

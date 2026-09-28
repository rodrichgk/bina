#ifndef PIANOROLL_H
#define PIANOROLL_H

#include "../core/projectmodel.h"

#include <QPointer>
#include <QSet>
#include <QVariantAnimation>
#include <QWidget>

class QComboBox;
class QLabel;
class QScrollArea;

namespace PianoRoll {

constexpr int LowestPitch = 21;   // A0
constexpr int HighestPitch = 108; // C8
constexpr int KeyHeight = 16;
constexpr int KeyboardWidth = 72;
constexpr int RulerHeight = 26;

QString pitchName(int pitch); // "C4", "F#3"...
bool isBlackKey(int pitch);

} // namespace PianoRoll

// The note grid. Holds a working copy of the clip's notes while editing and
// commits them to the model when a gesture ends.
class PianoRollCanvas : public QWidget
{
    Q_OBJECT

public:
    PianoRollCanvas(ProjectModel* model, int clipId, QWidget* parent = nullptr);

    void setSnap(double beats) { m_snap = beats; }
    void setPlayheadBeat(double beat);
    double pixelsPerBeat() const { return m_pxPerBeat; }
    double lengthBeats() const { return m_lengthBeats; }
    void reloadFromModel();

    QSize sizeHint() const override;

signals:
    void previewRequested(int pitch, int velocity);
    void zoomChanged(double pixelsPerBeat, double anchorBeat, int anchorViewportX);
    void geometryChanged(); // width or clip length changed; keyboard/ruler repaint

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Drag { None, Move, Resize, Select, Erase };

    int pitchAt(qreal y) const;
    qreal yOf(int pitch) const;
    double beatAt(qreal x) const { return x / m_pxPerBeat; }
    double snapDown(double beat) const;
    double snapNearest(double beat) const;
    QRectF noteRect(const Note& note) const;
    int noteAt(const QPointF& pos) const;
    bool onResizeEdge(const QPointF& pos, int index) const;
    void eraseAt(const QPointF& pos);
    void commit();
    void updateSize();
    void popNote(int index);              // New note grows in
    void leaveGhost(const QRectF& rect);  // Deleted note fades out

    ProjectModel* m_model;
    int m_clipId;
    QVector<Note> m_notes;
    double m_lengthBeats = 4.0;
    QColor m_color;

    double m_pxPerBeat = 96.0;
    double m_snap = 0.25;
    double m_lastLength = 1.0;
    double m_playheadBeat = -1.0;

    QSet<int> m_selected;
    Drag m_drag = Drag::None;
    QPointF m_pressPos;
    QVector<Note> m_dragOrigin; // notes as they were when the drag started
    int m_dragNote = -1;
    QRectF m_selectionRect;
    int m_lastPreviewPitch = -1;

    // Motion
    int m_popIndex = -1;
    qreal m_popProgress = 1.0;
    QList<QRectF> m_ghosts;
    qreal m_ghostOpacity = 0.0;
    QPointer<QVariantAnimation> m_ghostAnimation;
};

// Keyboard column; click a key to hear it
class PianoKeyboard : public QWidget
{
    Q_OBJECT

public:
    explicit PianoKeyboard(QWidget* parent = nullptr);
    void setOffset(int y);

signals:
    void keyPressed(int pitch);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int m_offset = 0;
    int m_pressedPitch = -1;
};

// Bars and beats across the top; click to move the song position
class PianoRuler : public QWidget
{
    Q_OBJECT

public:
    explicit PianoRuler(PianoRollCanvas* canvas, QWidget* parent = nullptr);
    void setOffset(int x);

signals:
    void beatClicked(double beat);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    PianoRollCanvas* m_canvas;
    int m_offset = 0;
};

// Separate window (like FL Studio) editing one note clip
class PianoRollWindow : public QWidget
{
    Q_OBJECT

public:
    PianoRollWindow(ProjectModel* model, int clipId, QWidget* parent = nullptr);

    int clipId() const { return m_clipId; }
    void setSongPosition(double seconds);

signals:
    void previewRequested(int track, int pitch, int velocity);
    void seekRequested(double seconds);

private:
    void updateTitle();

    ProjectModel* m_model;
    int m_clipId;
    PianoRollCanvas* m_canvas;
    PianoKeyboard* m_keyboard;
    PianoRuler* m_ruler;
    QScrollArea* m_scroll;
    QComboBox* m_snapCombo;
    QLabel* m_trackLabel;
};

#endif // PIANOROLL_H

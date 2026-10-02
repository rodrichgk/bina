#include "timelinewidget.h"
#include "musicalgrid.h"
#include "timelineruler.h"
#include "timelineview.h"
#include "tracksettingsdialog.h"
#include "../appconfig.h"
#include "../audio/audiodecoder.h"
#include "../audio/instruments/instrumentapi.h"
#include "../theme.h"
#include "../widgets/brandmark.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <cmath>

TimelineWidget::TimelineWidget(ProjectModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_trackHeight(AppConfig::instance().getTrackHeight())
    , m_trackIdWidth(AppConfig::instance().getTrackIdWidth())
{
    setupUi();
    setupIndicator();

    connect(m_model, &ProjectModel::projectReset, this, &TimelineWidget::onProjectReset);
    connect(m_model, &ProjectModel::trackAdded, this, &TimelineWidget::onTrackAdded);
    connect(m_model, &ProjectModel::trackChanged, this, &TimelineWidget::onTrackChanged);
    connect(m_model, &ProjectModel::clipAdded, this, &TimelineWidget::onClipAdded);
    connect(m_model, &ProjectModel::clipChanged, this, &TimelineWidget::onClipChanged);
    connect(m_model, &ProjectModel::clipRemoved, this, &TimelineWidget::onClipRemoved);
    // Bars move when the tempo changes
    connect(m_model, &ProjectModel::tempoChanged, this, &TimelineWidget::relayout);

    // Build rows for whatever the model already holds
    for (int i = 0; i < m_model->trackCount(); ++i) {
        onTrackAdded(i);
    }
    for (int id : m_model->clipIds()) {
        onClipAdded(id);
    }
    updateSceneSize();
    updateEmptyState();

    m_scrollTimer = new QTimer(this);
    connect(m_scrollTimer, &QTimer::timeout, this, &TimelineWidget::performScroll);
    m_scrollTimer->start(20);
}

// ---------------------------------------------------------------------------
// Setup

void TimelineWidget::setupUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);
    layout->addWidget(m_splitter);

    setupTrackList();
    setupView();

    // Left column: corner above the headers (level with the ruler), then the headers
    QWidget* left = new QWidget();
    left->setFixedWidth(m_trackIdWidth); // Headers keep their width; extra space goes to the timeline
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    BrandMark* corner = new BrandMark(); // The Bina lockup, where Butu keeps its brand
    corner->setFixedHeight(TimelineRuler::Height);
    leftLayout->addWidget(corner);
    leftLayout->addWidget(m_trackList);

    // Right column: ruler (fixed) above the lanes (scrolling)
    QWidget* right = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_ruler);
    // The empty-state hint overlays the view in the same grid cell. It can't live inside
    // the viewport: scrolling moves the viewport's child widgets along with the content.
    QGridLayout* stack = new QGridLayout();
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(m_view, 0, 0);
    stack->addWidget(m_emptyHint, 0, 0, Qt::AlignCenter);
    rightLayout->addLayout(stack, 1);

    m_splitter->addWidget(left);
    m_splitter->addWidget(right);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
}

void TimelineWidget::setupTrackList()
{
    m_trackList = new QListWidget();
    m_trackList->setFixedWidth(m_trackIdWidth);
    m_trackList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Follows the timeline scroll
    m_trackList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_trackList->setSelectionMode(QAbstractItemView::NoSelection);
    m_trackList->setSpacing(0);
    m_trackList->setFrameShape(QFrame::NoFrame);
    m_trackList->setFocusPolicy(Qt::NoFocus);
    m_trackList->setStyleSheet(QString(
        "QListWidget { border: none; border-radius: 0; background: %1; }"
        "QListWidget::item { padding: 0; border: none; }").arg(Theme::Surface.name()));
}

void TimelineWidget::setupView()
{
    m_scene = new QGraphicsScene(this);
    m_view = new TimelineView(m_scene, m_model, &m_scale);
    m_view->setMinimumWidth(400);
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::Antialiasing);
    connect(m_view, &TimelineView::zoomRequested, this, &TimelineWidget::zoom);

    m_ruler = new TimelineRuler(m_model, &m_scale);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, m_ruler, &TimelineRuler::setOffset);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &TimelineWidget::onTimelineScrolled);
    connect(m_ruler, &TimelineRuler::seekRequested, this, [this](double seconds) {
        setIndicatorPosition(seconds);
        emit indicatorPositionChanged(seconds);
    });

    // Empty state, shown until the first clip lands on the timeline
    m_emptyHint = new QLabel();
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyHint->setTextFormat(Qt::RichText);
    m_emptyHint->setText(QString(
        "<div style='color:%1; font-size:11pt; font-weight:600;'>Nothing on the timeline yet</div>"
        "<div style='color:%2; margin-top:6px;'>Drop audio files here, press <b style='color:%1'>Ctrl+I</b>, "
        "or double-click a lane to write notes</div>")
        .arg(Theme::TextDim.name(), Theme::TextFaint.name()));
    m_emptyHintOpacity = new QGraphicsOpacityEffect(m_emptyHint);
    m_emptyHintOpacity->setOpacity(1.0);
    m_emptyHint->setGraphicsEffect(m_emptyHintOpacity);

    // Audio files can be dropped straight onto the lanes; double-click creates note clips
    m_view->setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);
    m_view->viewport()->installEventFilter(this);

    m_dropHighlight = new QGraphicsRectItem();
    m_dropHighlight->setPen(QPen(Theme::Accent, 1.0));
    m_dropHighlight->setBrush(QColor(Theme::Accent.red(), Theme::Accent.green(), Theme::Accent.blue(), 28));
    m_dropHighlight->setZValue(50);
    m_dropHighlight->hide();
    m_scene->addItem(m_dropHighlight);
}

void TimelineWidget::setupIndicator()
{
    m_indicator = new TimelineIndicator(m_scene->height());
    m_indicator->setZValue(100);
    m_scene->addItem(m_indicator);
    m_indicator->setPos(0, 0);
    connect(m_indicator, &TimelineIndicator::indicatorMoved, this, &TimelineWidget::onIndicatorMoved);
}

// ---------------------------------------------------------------------------
// Zoom & layout

void TimelineWidget::zoom(double factor, int viewportX)
{
    // Keep the moment under the cursor under the cursor
    QScrollBar* bar = m_view->horizontalScrollBar();
    const double anchor = m_scale.toSeconds(bar->value() + viewportX);
    const double before = m_scale.pixelsPerSecond();
    m_scale.setPixelsPerSecond(before * factor);
    if (qFuzzyCompare(before, m_scale.pixelsPerSecond())) {
        return;
    }
    relayout();
    bar->setValue(int(m_scale.toX(anchor)) - viewportX);
}

void TimelineWidget::relayout()
{
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        if (const ClipData* clip = m_model->clip(it.key())) {
            refreshRegionContent(it.value(), *clip);
            placeRegion(it.value(), *clip);
        }
    }
    m_indicator->setPos(m_scale.toX(indicatorPosition()), 0);
    updateSceneSize();
    m_ruler->update();
    m_view->viewport()->update(); // Grid is drawn by the view
}

void TimelineWidget::updateSceneSize()
{
    const double barSeconds = ProjectModel::BeatsPerBar * m_model->secondsPerBeat();
    const double seconds = qMax(MinimumBars * barSeconds, m_model->length() + TrailingBars * barSeconds);
    const qreal width = m_scale.toX(seconds);
    const qreal height = qMax<qreal>(laneY(m_model->trackCount()), 1);
    m_scene->setSceneRect(0, 0, width, height);
    for (TrackLane* lane : std::as_const(m_lanes)) {
        lane->setWidth(width);
    }
    m_indicator->setHeight(height);
}

double TimelineWidget::snap(double seconds) const
{
    const double spb = m_model->secondsPerBeat();
    return MusicalGrid::snapSeconds(seconds, MusicalGrid::stepBeats(m_scale.toX(spb)), spb);
}

// ---------------------------------------------------------------------------
// Model -> view

void TimelineWidget::onProjectReset()
{
    // A new or opened project: drop every row and clip, then build from the model
    for (ClipRegion* region : std::as_const(m_regions)) {
        m_scene->removeItem(region);
        delete region;
    }
    m_regions.clear();
    for (TrackLane* lane : std::as_const(m_lanes)) {
        m_scene->removeItem(lane);
        delete lane;
    }
    m_lanes.clear();
    m_trackList->clear(); // Deletes the header widgets with their rows
    m_headers.clear();

    for (int i = 0; i < m_model->trackCount(); ++i) {
        onTrackAdded(i);
    }
    for (int id : m_model->clipIds()) {
        onClipAdded(id);
    }
    updateSceneSize();
    updateEmptyState();
    setIndicatorPosition(0.0);
    m_view->horizontalScrollBar()->setValue(0);
    m_view->verticalScrollBar()->setValue(0);
}

void TimelineWidget::onTrackAdded(int index)
{
    TrackHeaderWidget* header = new TrackHeaderWidget(index, m_model->track(index).name, this);
    connect(header, &TrackHeaderWidget::muteToggled, this, [this, index](bool muted) {
        m_model->setTrackMuted(index, muted);
    });
    connect(header, &TrackHeaderWidget::settingsRequested, this, &TimelineWidget::openTrackSettingsDialog);

    QListWidgetItem* item = new QListWidgetItem(m_trackList);
    item->setSizeHint(QSize(m_trackIdWidth, m_trackHeight));
    m_trackList->setItemWidget(item, header);
    m_headers.append(header);
    refreshHeader(index);

    TrackLane* lane = new TrackLane(m_trackHeight, m_scene->width());
    lane->setPos(0, laneY(index));
    m_scene->addItem(lane);
    m_lanes.append(lane);
    updateSceneSize();

    // Existing clips can now be dragged onto the new lane
    for (ClipRegion* region : std::as_const(m_regions)) {
        region->setLaneLayout(0, m_trackHeight, m_model->trackCount());
    }
}

void TimelineWidget::onTrackChanged(int index)
{
    if (index < 0 || index >= m_headers.size()) {
        return;
    }
    refreshHeader(index);
    // Color changes restyle this track's clips; mute/solo can change audibility on every track
    refreshRegionLooks();
}

void TimelineWidget::refreshHeader(int index)
{
    const TrackData& track = m_model->track(index);
    TrackHeaderWidget* header = m_headers[index];
    header->setTrackName(track.name);
    header->setMuted(track.muted);
    header->setColor(track.color);

    // Tracks with notes say which instrument plays them
    QString subtitle;
    if (m_model->trackHasNotes(index)) {
        const InstrumentSettings& inst = track.instrument;
        const InstrumentDefinition* def = InstrumentRegistry::find(inst.type);
        subtitle = def ? def->name : QString("Instrument");
        if (def && def->usesSample) {
            subtitle += inst.samplePath.isEmpty()
                ? QString(", no sample")
                : QString(", %1").arg(QFileInfo(inst.samplePath).completeBaseName());
        }
    }
    header->setSubtitle(subtitle);
}

void TimelineWidget::refreshAllHeaders()
{
    for (int i = 0; i < m_headers.size(); ++i) {
        refreshHeader(i);
    }
}

void TimelineWidget::onClipAdded(int id)
{
    const ClipData* clip = m_model->clip(id);
    if (!clip) {
        return;
    }
    ClipRegion* region = new ClipRegion(id, m_model->track(clip->track).color, m_trackHeight, &m_scale);
    region->setLaneLayout(0, m_trackHeight, m_model->trackCount());
    region->setSnap([this](double seconds) { return snap(seconds); });
    m_scene->addItem(region);
    m_regions.insert(id, region);
    refreshRegionContent(region, *clip);
    placeRegion(region, *clip);

    // New clips fade in and settle from a hair smaller: shows where the clip went
    region->setTransformOriginPoint(0, m_trackHeight / 2.0);
    Theme::animate(region, 0.0, 1.0, 200, [region](qreal v) {
        region->setOpacity(v);
        region->setScale(0.96 + 0.04 * v);
    });

    connect(region, &ClipRegion::moveRequested, this, &TimelineWidget::onRegionMoveRequested);
    connect(region, &ClipRegion::editRequested, this, &TimelineWidget::clipEditRequested);
    // Queued: the request comes from inside the region's own event handler
    connect(region, &ClipRegion::removeRequested, this, &TimelineWidget::onRegionRemoveRequested,
            Qt::QueuedConnection);

    updateSceneSize();
    updateEmptyState();
    refreshHeader(clip->track);
}

void TimelineWidget::onClipChanged(int id)
{
    const ClipData* clip = m_model->clip(id);
    ClipRegion* region = m_regions.value(id);
    if (clip && region) {
        refreshRegionContent(region, *clip);
        placeRegion(region, *clip);
        updateSceneSize();
        refreshAllHeaders(); // The clip may have moved between tracks
    }
}

void TimelineWidget::onClipRemoved(int id)
{
    ClipRegion* region = m_regions.take(id);
    if (!region) {
        return;
    }
    // Removed clips fade out before they're deleted
    region->setEnabled(false);
    QPointer<ClipRegion> fading(region);
    Theme::animate(this, region->opacity(), 0.0, 150,
                   [fading](qreal v) { if (fading) fading->setOpacity(v); },
                   [this, fading]() {
                       if (fading) {
                           m_scene->removeItem(fading);
                           fading->deleteLater();
                       }
                   });
    updateEmptyState();
    refreshAllHeaders();
}

void TimelineWidget::refreshRegionContent(ClipRegion* region, const ClipData& clip)
{
    region->setDuration(m_model->clipDuration(clip));
    if (clip.isNoteClip()) {
        region->setNotes(clip.notes, clip.lengthBeats);
    } else {
        region->setWaveform(clip.peaks);
    }
}

void TimelineWidget::placeRegion(ClipRegion* region, const ClipData& clip)
{
    region->setPos(m_scale.toX(clip.start), laneY(clip.track));
    // A clip always looks like the track it sits on, so moving it recolors it
    region->setColor(m_model->track(clip.track).color);
    region->setAudible(m_model->isTrackAudible(clip.track));
}

void TimelineWidget::refreshRegionLooks()
{
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        if (const ClipData* clip = m_model->clip(it.key())) {
            it.value()->setColor(m_model->track(clip->track).color);
            it.value()->setAudible(m_model->isTrackAudible(clip->track));
        }
    }
}

void TimelineWidget::createNoteClipAt(const QPointF& scenePos)
{
    const int lane = laneAt(scenePos.y());
    if (!m_model->isValidTrack(lane)) {
        return;
    }
    // New clips start on the beat under the cursor and are one bar long
    const double spb = m_model->secondsPerBeat();
    const double start = std::floor(m_scale.toSeconds(qMax(0.0, scenePos.x())) / spb) * spb;
    const int id = m_model->addNoteClip(lane, start, ProjectModel::BeatsPerBar);
    if (id >= 0) {
        emit clipEditRequested(id);
    }
}

// ---------------------------------------------------------------------------
// View -> model

void TimelineWidget::onRegionMoveRequested(int clipId, int track, double startSeconds)
{
    m_model->moveClip(clipId, track, startSeconds);
    // Snap back to the model's position if it clamped or ignored the move
    if (const ClipData* clip = m_model->clip(clipId)) {
        if (ClipRegion* region = m_regions.value(clipId)) {
            placeRegion(region, *clip);
        }
    }
}

void TimelineWidget::onRegionRemoveRequested(int clipId)
{
    m_model->removeClip(clipId);
}

void TimelineWidget::openTrackSettingsDialog(int trackIndex)
{
    if (!m_model->isValidTrack(trackIndex)) {
        return;
    }
    TrackSettingsDialog dialog(m_model, trackIndex, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Empty state

void TimelineWidget::updateEmptyState()
{
    if (!m_emptyHint) {
        return;
    }
    // The hint fades out as the first clip lands, and back in when the timeline empties
    const bool show = m_regions.isEmpty();
    if (show == m_emptyHint->isVisible() && m_emptyHintOpacity->opacity() == (show ? 1.0 : 0.0)) {
        return;
    }
    if (show) {
        m_emptyHint->show();
    }
    Theme::animate(m_emptyHint, m_emptyHintOpacity->opacity(), show ? 1.0 : 0.0, show ? 220 : 160,
                   [this](qreal v) { m_emptyHintOpacity->setOpacity(v); },
                   [this, show]() { m_emptyHint->setVisible(show); });
}

// ---------------------------------------------------------------------------
// Playhead

void TimelineWidget::setIndicatorPosition(double seconds)
{
    if (!m_indicator) {
        return;
    }
    const qreal x = m_scale.toX(seconds);
    m_indicator->setPos(x, 0); // setPos never emits indicatorMoved, so no feedback loop
    m_ruler->setPlayhead(seconds);

    // Page the view along when the playhead leaves it; the page glides so the eye can follow
    if (m_pageAnimation) {
        return;
    }
    QScrollBar* bar = m_view->horizontalScrollBar();
    const qreal left = bar->value();
    const qreal right = left + m_view->viewport()->width();
    const qreal margin = m_view->viewport()->width() * 0.08;
    if (x < left || x > right - margin) {
        const int target = qBound(bar->minimum(), int(x - margin), bar->maximum());
        const int from = bar->value();
        m_pageAnimation = Theme::animate(this, 0.0, 1.0, 240, [bar, from, target](qreal v) {
            bar->setValue(from + int((target - from) * v));
        });
    }
}

double TimelineWidget::indicatorPosition() const
{
    return m_indicator ? m_scale.toSeconds(m_indicator->scenePos().x()) : 0.0;
}

void TimelineWidget::setPlaybackMode(bool isPlaying)
{
    m_isPlaybackMode = isPlaying;
}

void TimelineWidget::onIndicatorMoved(TimelineIndicator* indicator)
{
    Q_UNUSED(indicator)
    m_ruler->setPlayhead(indicatorPosition());
    emit indicatorPositionChanged(indicatorPosition());
    focusOnItem(m_indicator);
}

// ---------------------------------------------------------------------------
// Drag & drop, double-click

QStringList TimelineWidget::droppableFiles(const QMimeData* mime) const
{
    QStringList files;
    if (!mime || !mime->hasUrls()) {
        return files;
    }
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile() && AudioDecoder::isSupportedFile(url.toLocalFile())) {
            files << url.toLocalFile();
        }
    }
    return files;
}

int TimelineWidget::laneAt(qreal sceneY) const
{
    return qMax(0, int(sceneY / m_trackHeight));
}

void TimelineWidget::showDropTarget(const QPointF& scenePos, int fileCount)
{
    // One lane per file from the hovered lane down; the left edge marks the (snapped) drop time
    const qreal x = m_scale.toX(qMax(0.0, snap(m_scale.toSeconds(scenePos.x()))));
    const int lane = laneAt(scenePos.y());
    m_dropHighlight->setRect(x, laneY(lane) + 1.5, m_scale.toX(4 * m_model->secondsPerBeat()),
                             fileCount * m_trackHeight - 3);
    m_dropHighlight->show();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_view->viewport()) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonDblClick: {
        // Double-click on an empty spot of a lane creates a note clip there
        auto* e = static_cast<QMouseEvent*>(event);
        bool onClip = false;
        for (QGraphicsItem* item : m_view->items(e->position().toPoint())) {
            onClip = onClip || dynamic_cast<ClipRegion*>(item);
        }
        if (e->button() == Qt::LeftButton && !onClip) {
            createNoteClipAt(m_view->mapToScene(e->position().toPoint()));
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    case QEvent::DragEnter: {
        auto* e = static_cast<QDragEnterEvent*>(event);
        m_dragFileCount = droppableFiles(e->mimeData()).size();
        if (m_dragFileCount == 0) {
            e->ignore();
            return true;
        }
        e->acceptProposedAction();
        showDropTarget(m_view->mapToScene(e->position().toPoint()), m_dragFileCount);
        return true;
    }
    case QEvent::DragMove: {
        auto* e = static_cast<QDragMoveEvent*>(event);
        if (m_dragFileCount == 0) {
            e->ignore();
            return true;
        }
        e->acceptProposedAction();
        showDropTarget(m_view->mapToScene(e->position().toPoint()), m_dragFileCount);
        return true;
    }
    case QEvent::DragLeave:
        m_dropHighlight->hide();
        m_dragFileCount = 0;
        return true;
    case QEvent::Drop: {
        auto* e = static_cast<QDropEvent*>(event);
        m_dropHighlight->hide();
        const QStringList files = droppableFiles(e->mimeData());
        m_dragFileCount = 0;
        if (files.isEmpty()) {
            e->ignore();
            return true;
        }
        e->acceptProposedAction();
        const QPointF scenePos = m_view->mapToScene(e->position().toPoint());
        // All dropped URLs go through, so unsupported ones can be reported
        QStringList all;
        for (const QUrl& url : e->mimeData()->urls()) {
            if (url.isLocalFile()) {
                all << url.toLocalFile();
            }
        }
        emit filesDropped(all, laneAt(scenePos.y()), qMax(0.0, snap(m_scale.toSeconds(scenePos.x()))));
        return true;
    }
    default:
        return QWidget::eventFilter(watched, event);
    }
}

// ---------------------------------------------------------------------------
// Scrolling

void TimelineWidget::focusOnItem(QGraphicsItem* item)
{
    // Auto-scroll while dragging the playhead past either edge
    if (!item || !m_view) {
        return;
    }
    const QPoint p = m_view->mapFromScene(item->scenePos());
    m_scrollLeft = p.x() <= 0;
    m_scrollRight = p.x() >= m_view->viewport()->width();
}

void TimelineWidget::performScroll()
{
    static int stepSize = 1;
    if (m_scrollLeft || m_scrollRight) {
        stepSize = qMin(stepSize + 1, 10);
        QScrollBar* bar = m_view->horizontalScrollBar();
        bar->setValue(bar->value() + (m_scrollRight ? stepSize : -stepSize));
    } else {
        stepSize = 1;
    }
}

void TimelineWidget::onTimelineScrolled(int value)
{
    // Keep the header column aligned with the lanes
    if (m_trackList->count() == 0) {
        return;
    }
    const int topIndex = qMin(value / m_trackHeight, m_trackList->count() - 1);
    if (QListWidgetItem* top = m_trackList->item(topIndex)) {
        m_trackList->scrollToItem(top, QAbstractItemView::PositionAtTop);
        QScrollBar* bar = m_trackList->verticalScrollBar();
        bar->setValue(bar->value() + value % m_trackHeight);
    }
}

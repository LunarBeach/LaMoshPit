#include "gui/sequencer/SequencerDock.h"
#include "gui/sequencer/SequencerPreviewPlayer.h"
#include "gui/sequencer/SequencerTimelineView.h"
#include "gui/sequencer/SequencerTrackHeader.h"
#include "gui/sequencer/SequencerTransitionPanel.h"
#include "gui/sequencer/SequencerClipPropertiesPanel.h"
#include "core/sequencer/ClipEffects.h"
#include "core/sequencer/EditCommand.h"
#include "gui/sequencer/SequencerRenderDialog.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerPlaybackClock.h"
#include "core/sequencer/FrameRouter.h"
#include "core/sequencer/SpoutSender.h"
#include "core/sequencer/EditCommand.h"
#include "core/sequencer/Tick.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QListWidget>
#include <QMimeData>
#include <QShortcut>
#include <QKeySequence>
#include <QComboBox>
#include <QSignalBlocker>
#include <QApplication>
#include <QKeyEvent>
#include <QFileInfo>
#include <QThread>

namespace sequencer {
namespace {

// ── EffectsRackList ────────────────────────────────────────────────────────
// Tiny QListWidget subclass that packages the selected item's effect id
// into the custom drag MIME (kClipEffectMimeType) so the timeline view can
// identify the dropped effect without having to parse Qt's internal
// item-model MIME.  The drag source is the only place that speaks this
// MIME; the drop target lives in SequencerTimelineView.
// ──────────────────────────────────────────────────────────────────────────
class EffectsRackList : public QListWidget {
public:
    explicit EffectsRackList(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

protected:
    QStringList mimeTypes() const override {
        return { QString::fromLatin1(kClipEffectMimeType) };
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* md = new QMimeData;
        if (!items.isEmpty()) {
            const QString id = items.first()->data(Qt::UserRole).toString();
            if (!id.isEmpty())
                md->setData(kClipEffectMimeType, id.toUtf8());
        }
        return md;
    }
};

} // namespace

SequencerDock::SequencerDock(SequencerProject* project, QWidget* parent)
    : QDockWidget("NLE Sequencer", parent)
    , m_project(project)
{
    setObjectName("SequencerDock");
    setFeatures(QDockWidget::DockWidgetClosable
              | QDockWidget::DockWidgetMovable
              | QDockWidget::DockWidgetFloatable);
    setAllowedAreas(Qt::AllDockWidgetAreas);

    auto* root = new QWidget(this);
    auto* vbox = new QVBoxLayout(root);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // ── Preview / transition panel side-by-side ────────────────────────────
    auto* topHost   = new QWidget(root);
    auto* topLayout = new QHBoxLayout(topHost);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(4);

    m_preview = new SequencerPreviewPlayer(topHost);
    topLayout->addWidget(m_preview, /*stretch=*/3);

    auto* transitionBox = new QFrame(topHost);
    transitionBox->setFrameShape(QFrame::StyledPanel);
    transitionBox->setMinimumWidth(240);
    auto* transitionBoxLayout = new QVBoxLayout(transitionBox);
    transitionBoxLayout->setContentsMargins(4, 4, 4, 4);
    auto* transitionTitle = new QLabel("Live Transition", transitionBox);
    transitionTitle->setStyleSheet(
        "QLabel { color:#ddd; font:bold 9pt 'Segoe UI'; padding:2px; }");
    transitionBoxLayout->addWidget(transitionTitle);
    // Output mode — LayerComposite (authoring, every track visible with
    // its compositor properties) vs LiveVJ (single-active-track routing
    // with hotkey-triggered transitions).  Placed first in the column
    // because it gates whether the rest of the live-VJ controls below
    // actually do anything.
    auto* composeRow = new QHBoxLayout;
    auto* composeLabel = new QLabel("Output Mode:", transitionBox);
    composeLabel->setStyleSheet("QLabel { color:#ddd; font:9pt 'Segoe UI'; }");
    m_composeMode = new QComboBox(transitionBox);
    m_composeMode->addItem("Render Preview (layer composite)",
                           int(FrameRouter::ComposeMode::LayerComposite));
    m_composeMode->addItem("Live VJ (active track + transitions)",
                           int(FrameRouter::ComposeMode::LiveVJ));
    m_composeMode->setToolTip(
        "Render Preview: every enabled track is composited bottom-up using\n"
        "  each clip's opacity / blend mode / fade envelope.  The preview\n"
        "  reflects exactly what the offline renderer will bake.  Hotkey\n"
        "  switches and live transitions are inert.\n\n"
        "Live VJ: single-active-track routing.  Number keys 1-9 cue track\n"
        "  changes via the active transition (Switch mode) or play a track\n"
        "  while held (Touch mode).  Clip opacity / blend mode / fades are\n"
        "  ignored — use Render Preview when authoring for rendering.");
    composeRow->addWidget(composeLabel);
    composeRow->addWidget(m_composeMode, /*stretch=*/1);
    transitionBoxLayout->addLayout(composeRow);

    // Sequence frame rate — the timeline's authoritative playback + render
    // cadence.  Mixed-fps source clips are sampled to this rate via each
    // chain's catch-up loop (60 → 30 drops every other frame; 24 → 30
    // holds some frames across two ticks).  Changing this drives both the
    // preview clock and the offline render output fps.
    auto* fpsRow   = new QHBoxLayout;
    auto* fpsLabel = new QLabel("Sequence FPS:", transitionBox);
    fpsLabel->setStyleSheet("QLabel { color:#ddd; font:9pt 'Segoe UI'; }");
    m_seqFps = new QComboBox(transitionBox);
    // AVRational entries stored as (num, den) packed into a QPair so the
    // combo's data round-trips exactly — 23.976 = 24000/1001, not a float.
    auto addFps = [this](const char* label, int num, int den) {
        m_seqFps->addItem(QString::fromLatin1(label),
                          QVariant::fromValue(QPoint(num, den)));
    };
    addFps("23.976",  24000, 1001);
    addFps("24",         24,    1);
    addFps("25",         25,    1);
    addFps("29.97",  30000, 1001);
    addFps("30",         30,    1);
    addFps("50",         50,    1);
    addFps("59.94",  60000, 1001);
    addFps("60",         60,    1);
    m_seqFps->setToolTip(
        "The sequence's authoritative playback + render frame rate.\n"
        "Source clips at different frame rates are sampled to this rate\n"
        "(e.g. a 60fps source on a 30fps sequence contributes every other\n"
        "frame to the preview AND the rendered output).  Change this to\n"
        "match your delivery target BEFORE rendering.");
    fpsRow->addWidget(fpsLabel);
    fpsRow->addWidget(m_seqFps, /*stretch=*/1);
    transitionBoxLayout->addLayout(fpsRow);

    m_transitionPanel = new SequencerTransitionPanel(transitionBox);
    transitionBoxLayout->addWidget(m_transitionPanel);

    // Hotkey mode — Switch vs Touch.  See FrameRouter::HotkeyMode for
    // semantics.  Placed in the transition box because it's a live-
    // -performance control, like the transition type / duration above.
    auto* modeRow = new QHBoxLayout;
    auto* modeLabel = new QLabel("Hotkey Mode:", transitionBox);
    modeLabel->setStyleSheet("QLabel { color:#ddd; font:9pt 'Segoe UI'; }");
    m_hotkeyMode = new QComboBox(transitionBox);
    m_hotkeyMode->addItem("Switch (press = stay)", int(FrameRouter::HotkeyMode::Switch));
    m_hotkeyMode->addItem("Touch  (hold to play)", int(FrameRouter::HotkeyMode::Touch));
    m_hotkeyMode->setToolTip(
        "Switch: press 1-9 to trigger the active transition to that track.\n"
        "Touch:  hold 1-9 to route that track live; release returns to the\n"
        "         topmost track with content at the playhead.\n"
        "(Both are inert while Output Mode is Render Preview.)");
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(m_hotkeyMode, /*stretch=*/1);
    transitionBoxLayout->addLayout(modeRow);

    // Spout output toggle + status — "live output" UX lives here because
    // it pairs conceptually with the transition settings above.
    auto* spoutRow = new QHBoxLayout;
    m_chkSpout = new QCheckBox("Spout Out", transitionBox);
    m_chkSpout->setToolTip(
        "Publish the router's output as a Spout sender named \"LaMoshPit\".\n"
        "OBS (with the Spout2 plugin) picks it up as a live video source.");
    m_spoutStatus = new QLabel("(off)", transitionBox);
    m_spoutStatus->setStyleSheet("QLabel { color:#888; font:8pt 'Segoe UI'; }");
    spoutRow->addWidget(m_chkSpout);
    spoutRow->addWidget(m_spoutStatus, /*stretch=*/1);
    transitionBoxLayout->addLayout(spoutRow);

    // ── Effects Rack ─────────────────────────────────────────────────────
    // Palette of image effects that the user drags onto a clip in the
    // timeline to apply.  Render-time feature (same scope as Clip
    // Properties below) — effects run during the LayerComposite preview
    // and the offline render.  Extensible: adding a new effect means
    // extending the ClipEffect enum + availableClipEffects() + the
    // applyClipEffects() switch.  UI picks everything up automatically.
    {
        auto* fxSep = new QFrame(transitionBox);
        fxSep->setFrameShape(QFrame::HLine);
        fxSep->setFrameShadow(QFrame::Sunken);
        fxSep->setStyleSheet("QFrame { color:#333; }");
        transitionBoxLayout->addWidget(fxSep);
    }
    auto* fxTitle = new QLabel("Effects Rack", transitionBox);
    fxTitle->setStyleSheet(
        "QLabel { color:#ddd; font:bold 9pt 'Segoe UI'; padding:2px; }");
    fxTitle->setToolTip("Drag an effect onto a clip in the timeline to apply it.");
    transitionBoxLayout->addWidget(fxTitle);

    auto* effectsRack = new EffectsRackList(transitionBox);
    effectsRack->setMaximumHeight(88);
    effectsRack->setToolTip("Drag onto a clip in the timeline to apply.");
    for (ClipEffect e : availableClipEffects()) {
        auto* item = new QListWidgetItem(clipEffectDisplayName(e), effectsRack);
        item->setData(Qt::UserRole, clipEffectId(e));
    }
    transitionBoxLayout->addWidget(effectsRack);

    // ── Clip Properties (render-time, stacked under Live Transition) ─────
    // Separator + heading so the render-only controls are visually distinct
    // from the live-VJ knobs above.  Same vertical column since both are
    // "inspector" surfaces for the sequencer.
    auto* sep = new QFrame(transitionBox);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setStyleSheet("QFrame { color:#333; }");
    transitionBoxLayout->addWidget(sep);

    auto* clipPropsTitle = new QLabel("Clip Properties (render)", transitionBox);
    clipPropsTitle->setStyleSheet(
        "QLabel { color:#ddd; font:bold 9pt 'Segoe UI'; padding:2px; }");
    clipPropsTitle->setToolTip(
        "Applies when baking the project with the Render button.\n"
        "Live VJ output via Spout is unaffected by these controls.");
    transitionBoxLayout->addWidget(clipPropsTitle);
    m_clipPropsPanel = new SequencerClipPropertiesPanel(transitionBox);
    transitionBoxLayout->addWidget(m_clipPropsPanel);

    transitionBoxLayout->addStretch(1);
    topLayout->addWidget(transitionBox, /*stretch=*/1);

    // ── Transport ──────────────────────────────────────────────────────────
    auto* transport = new QWidget(root);
    auto* tbar      = new QHBoxLayout(transport);
    tbar->setContentsMargins(0, 0, 0, 0);
    m_btnPlay    = new QPushButton("Play", transport);
    m_btnStop    = new QPushButton("Stop", transport);
    m_btnMarkIn  = new QPushButton("I (In)",  transport);
    m_btnMarkOut = new QPushButton("O (Out)", transport);
    m_chkLoop    = new QCheckBox("Loop",      transport);
    m_btnRender  = new QPushButton("Render\u2026", transport);
    m_btnRender->setToolTip("Export the NLE sequence to an MP4 file.");
    m_seek       = new QSlider(Qt::Horizontal, transport);
    m_seek->setRange(0, 0);
    m_timeLabel  = new QLabel("00:00.000 / 00:00.000", transport);
    tbar->addWidget(m_btnPlay);
    tbar->addWidget(m_btnStop);
    tbar->addWidget(m_btnMarkIn);
    tbar->addWidget(m_btnMarkOut);
    tbar->addWidget(m_chkLoop);
    tbar->addWidget(m_btnRender);
    tbar->addWidget(m_seek, /*stretch=*/1);
    tbar->addWidget(m_timeLabel);

    // ── Timeline host (header column + view) ───────────────────────────────
    auto* tlHost   = new QWidget(root);
    auto* tlLayout = new QHBoxLayout(tlHost);
    tlLayout->setContentsMargins(0, 0, 0, 0);
    tlLayout->setSpacing(0);

    // Engine — clock + router before timeline so the view can hook in.
    // The clock stays on the GUI thread (it owns a QTimer whose cadence
    // drives the preview).  The router moves to its own worker thread so
    // heavy per-tick compositing never blocks the GUI.  All signals
    // between clock ↔ router and router ↔ preview / Spout are
    // auto-connection → resolves to QueuedConnection across the thread
    // boundary, which is correct.  The router's own public API methods
    // (setComposeMode, requestTrackSwitch, etc.) internally marshal to
    // the router's thread via QMetaObject::invokeMethod.
    m_clock  = std::make_unique<SequencerPlaybackClock>(this);
    // Router is deliberately NOT parented — QObject's thread affinity is
    // moved with the object, but a parent enforces same-thread destruction
    // which conflicts with our moveToThread + deleteLater pattern.
    m_router = new FrameRouter(m_project, m_clock.get(), /*parent=*/nullptr);
    m_routerThread = new QThread(this);
    m_routerThread->setObjectName(QStringLiteral("FrameRouterThread"));
    m_router->moveToThread(m_routerThread);
    // Tear-down: when the thread finishes its event loop (triggered by
    // our destructor's quit()), delete the router.  The thread itself is
    // parented to `this` so it's destroyed during ~QDockWidget, after
    // our destructor has already called quit()+wait().
    connect(m_routerThread, &QThread::finished,
            m_router,       &QObject::deleteLater);
    m_routerThread->start();
    m_spout  = std::make_unique<SpoutSender>(this);

    m_trackHeader  = new SequencerTrackHeader(m_project, tlHost);
    m_timelineView = new SequencerTimelineView(m_project, m_clock.get(), tlHost);
    tlLayout->addWidget(m_trackHeader);
    tlLayout->addWidget(m_timelineView, /*stretch=*/1);

    // ── Splitter: top (preview+transition) / bottom (timeline) ─────────────
    auto* split = new QSplitter(Qt::Vertical, root);
    split->addWidget(topHost);
    split->addWidget(tlHost);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    vbox->addWidget(split, /*stretch=*/1);
    vbox->addWidget(transport);

    setWidget(root);

    // ── Signal wiring ──────────────────────────────────────────────────────
    connect(m_router, &FrameRouter::frameReady,
            m_preview,      &SequencerPreviewPlayer::onFrameReady);

    connect(m_clock.get(), &SequencerPlaybackClock::tickAdvanced,
            this,          &SequencerDock::onClockTickAdvanced);
    if (m_project) {
        connect(m_project, &SequencerProject::projectChanged,
                this,      &SequencerDock::onProjectChanged);
    }

    connect(m_btnPlay,    &QPushButton::clicked,
            this,         &SequencerDock::onPlayPauseClicked);
    connect(m_btnStop,    &QPushButton::clicked,
            this,         &SequencerDock::onStopClicked);
    connect(m_btnMarkIn,  &QPushButton::clicked,
            this,         &SequencerDock::onMarkIn);
    connect(m_btnMarkOut, &QPushButton::clicked,
            this,         &SequencerDock::onMarkOut);
    connect(m_chkLoop,    &QCheckBox::toggled,
            this,         &SequencerDock::onLoopToggled);
    connect(m_chkSpout,   &QCheckBox::toggled,
            this,         &SequencerDock::onSpoutToggled);
    connect(m_btnRender,  &QPushButton::clicked,
            this,         &SequencerDock::onRenderClicked);
    connect(m_seek,       &QSlider::sliderMoved,
            this,         &SequencerDock::onSeekSliderMoved);

    // Router frames fan out to both preview AND Spout (when enabled).
    connect(m_router, &FrameRouter::frameReady,
            m_spout.get(),  &SpoutSender::onFrameReady);

    // Transition panel → router.
    connect(m_transitionPanel, &SequencerTransitionPanel::typeChanged,
            m_router,    &FrameRouter::setTransitionTypeId);
    connect(m_transitionPanel, &SequencerTransitionPanel::paramsChanged,
            m_router,    &FrameRouter::setTransitionParams);
    // Prime router with the panel's initial values.
    m_router->setTransitionTypeId(m_transitionPanel->currentTypeId());
    m_router->setTransitionParams(m_transitionPanel->currentParams());

    // Clip Properties — timeline selection populates the panel, panel edits
    // flow back as ChangeClipPropertyCmd on the sequencer's own undo stack.
    connect(m_timelineView, &SequencerTimelineView::selectedClipChanged,
            this, [this](int trackIdx, int clipIdx) {
        if (!m_clipPropsPanel || !m_project) return;
        if (trackIdx < 0 || clipIdx < 0
            || trackIdx >= m_project->trackCount()) {
            m_clipPropsPanel->setSelection(-1, -1, nullptr);
            return;
        }
        const auto& track = m_project->track(trackIdx);
        if (clipIdx >= track.clips.size()) {
            m_clipPropsPanel->setSelection(-1, -1, nullptr);
            return;
        }
        m_clipPropsPanel->setSelection(trackIdx, clipIdx, &track.clips[clipIdx]);
    });
    connect(m_clipPropsPanel, &SequencerClipPropertiesPanel::propertiesEdited,
            this, [this](int trackIdx, int clipIdx,
                         float opacity, BlendMode blend,
                         Tick fadeIn, Tick fadeOut) {
        if (!m_project) return;
        m_project->executeCommand(std::make_unique<ChangeClipPropertyCmd>(
            trackIdx, clipIdx, opacity, blend, fadeIn, fadeOut));
    });
    connect(m_clipPropsPanel, &SequencerClipPropertiesPanel::effectsEdited,
            this, [this](int trackIdx, int clipIdx,
                         QVector<ClipEffect> effects) {
        if (!m_project) return;
        m_project->executeCommand(std::make_unique<ChangeClipEffectsCmd>(
            trackIdx, clipIdx, std::move(effects)));
    });

    // Hotkeys for I/O/L/Space — use QShortcut with WidgetWithChildrenShortcut
    // context so they fire when any descendant of the dock has focus.
    // Scope is dock-local on purpose: the main window has its own shortcuts
    // (Ctrl+Z, Ctrl+O, Delete etc. in the mosh editor).
    auto bindDockShortcut = [this](const QKeySequence& seq, auto fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    bindDockShortcut(QKeySequence(Qt::Key_I), [this]() { onMarkIn(); });
    bindDockShortcut(QKeySequence(Qt::Key_O), [this]() { onMarkOut(); });
    bindDockShortcut(QKeySequence(Qt::Key_L), [this]() {
        if (m_chkLoop) m_chkLoop->setChecked(!m_chkLoop->isChecked());
    });
    bindDockShortcut(QKeySequence(Qt::Key_Space),
                     [this]() { onPlayPauseClicked(); });

    // 1-9 track hotkeys need BOTH key-press and key-release, which QShortcut
    // cannot deliver.  Install an app-level event filter that only acts when
    // the focused widget is inside this dock — keeps other app shortcuts
    // (e.g. digit-keys in the mosh editor, if any) unaffected.
    qApp->installEventFilter(this);

    // Mode selector → router.
    connect(m_hotkeyMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_router || !m_hotkeyMode) return;
        const auto mode = static_cast<FrameRouter::HotkeyMode>(
            m_hotkeyMode->currentData().toInt());
        m_router->setHotkeyMode(mode);
    });

    // Output-mode selector → router.  Also prime the router with the
    // panel's current choice so the default matches the UI on first load.
    connect(m_composeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_router || !m_composeMode) return;
        const auto mode = static_cast<FrameRouter::ComposeMode>(
            m_composeMode->currentData().toInt());
        m_router->setComposeMode(mode);
    });
    m_router->setComposeMode(static_cast<FrameRouter::ComposeMode>(
        m_composeMode->currentData().toInt()));

    // Sequence FPS → project.  setOutputFrameRate already no-ops on same-
    // rate writes so wiring currentIndexChanged doesn't churn.  The
    // reverse direction (project → combo sync) is below.
    connect(m_seqFps, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_project || !m_seqFps) return;
        const QPoint packed = m_seqFps->currentData().toPoint();
        AVRational r { packed.x(), packed.y() };
        m_project->setOutputFrameRate(r);
    });
    // Sync combo to the current project fps on construction, and on every
    // future outputFrameRateChanged (e.g. project load).  Find the combo
    // entry whose packed (num, den) matches; fall back to item 0 (23.976)
    // if none match — keeps the UI coherent for legacy projects saved
    // with an off-preset rate.
    auto syncSeqFpsCombo = [this]() {
        if (!m_project || !m_seqFps) return;
        const AVRational r = m_project->outputFrameRate();
        QSignalBlocker b(m_seqFps);
        for (int i = 0; i < m_seqFps->count(); ++i) {
            const QPoint packed = m_seqFps->itemData(i).toPoint();
            if (packed.x() == r.num && packed.y() == r.den) {
                m_seqFps->setCurrentIndex(i);
                return;
            }
        }
    };
    if (m_project) {
        connect(m_project, &SequencerProject::outputFrameRateChanged,
                this, syncSeqFpsCombo);
    }
    syncSeqFpsCombo();

    // Dock-scoped undo/redo — fires only when focus is inside this dock
    // (Qt::WidgetWithChildrenShortcut).  The MB editor's Ctrl+Z lives on
    // the Edit menu with the default WindowShortcut context; that
    // narrower context wins here when the user is focused on sequencer
    // content, so the two stacks never cross-contaminate.
    {
        auto* undoSc = new QShortcut(QKeySequence::Undo, this);
        undoSc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(undoSc, &QShortcut::activated, this, [this]() {
            if (m_project) m_project->undo();
        });
        auto* redoSc1 = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), this);
        redoSc1->setContext(Qt::WidgetWithChildrenShortcut);
        connect(redoSc1, &QShortcut::activated, this, [this]() {
            if (m_project) m_project->redo();
        });
        auto* redoSc2 = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this);
        redoSc2->setContext(Qt::WidgetWithChildrenShortcut);
        connect(redoSc2, &QShortcut::activated, this, [this]() {
            if (m_project) m_project->redo();
        });
    }

    refreshDurationUi();
}

SequencerDock::~SequencerDock()
{
    if (qApp) qApp->removeEventFilter(this);

    // Shutdown order (important):
    //   1. Stop the clock so no further tickAdvanced fires cross-thread.
    //      Any already-queued ticks are discarded when the router's event
    //      loop exits below — slots posted to a dying QObject are dropped.
    //   2. Quit the router's event loop and wait.  On finished(), the
    //      previously-wired deleteLater deletes the router on its own
    //      thread before wait() returns — that's the canonical Qt idiom
    //      and avoids "delete a QObject from another thread" warnings.
    //   3. The QThread itself is parented to this QDockWidget and gets
    //      destroyed in the normal child-deletion chain below — safe
    //      because it's no longer running.
    if (m_clock) m_clock->stop();
    if (m_routerThread) {
        m_routerThread->quit();
        m_routerThread->wait();
    }
    m_router = nullptr;   // already deleteLater'd on finished()
}

// =============================================================================
// Router config persistence — forwarders so MainWindow can save/restore the
// router state through the dock without having to know about FrameRouter.
// =============================================================================

QJsonObject SequencerDock::routerConfigToJson() const
{
    return m_router ? m_router->configToJson() : QJsonObject();
}

void SequencerDock::routerConfigFromJson(const QJsonObject& obj)
{
    if (!m_router) return;
    m_router->configFromJson(obj);

    // Sync the dock's dropdowns to match the loaded router state.  Block
    // signals so re-setting the index doesn't bounce back through the
    // currentIndexChanged handler and re-push to the router.
    if (m_hotkeyMode) {
        QSignalBlocker b(m_hotkeyMode);
        const int idx = m_hotkeyMode->findData(int(m_router->hotkeyMode()));
        if (idx >= 0) m_hotkeyMode->setCurrentIndex(idx);
    }
    if (m_composeMode) {
        QSignalBlocker b(m_composeMode);
        const int idx = m_composeMode->findData(int(m_router->composeMode()));
        if (idx >= 0) m_composeMode->setCurrentIndex(idx);
    }
}

// =============================================================================
// eventFilter — app-level 1-9 press/release for Touch + Switch hotkey modes.
// =============================================================================

bool SequencerDock::eventFilter(QObject* /*watched*/, QEvent* event)
{
    const QEvent::Type t = event->type();
    if (t != QEvent::KeyPress && t != QEvent::KeyRelease) return false;

    // Only act when the focus chain is inside this dock — otherwise the
    // user is working elsewhere in the app and shouldn't be hijacked.
    QWidget* focus = QApplication::focusWidget();
    if (!focus) return false;
    if (focus != this && !isAncestorOf(focus)) return false;

    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->isAutoRepeat()) {
        // Auto-repeat on hold generates a stream of press events; in Touch
        // mode we've already "pressed" once on the first event.  Swallow
        // the repeats but don't fire the action again.
        return true;
    }

    const int key = ke->key();
    if (key < Qt::Key_1 || key > Qt::Key_9) return false;

    const int trackIdx = key - Qt::Key_1;
    if (!m_project || !m_router) return false;
    if (trackIdx >= m_project->trackCount()) return true;   // key out of range — consume

    if (m_router->hotkeyMode() == FrameRouter::HotkeyMode::Switch) {
        if (t == QEvent::KeyPress) m_router->requestTrackSwitch(trackIdx);
        // Release has no effect in Switch mode — consume to prevent any
        // default text-focus behaviour from inserting a digit somewhere.
        return true;
    }

    // Touch mode.
    if (t == QEvent::KeyPress)   m_router->requestTrackHoldPress(trackIdx);
    else                         m_router->requestTrackHoldRelease(trackIdx);
    return true;
}

// =============================================================================
// Transport
// =============================================================================

void SequencerDock::onPlayPauseClicked()
{
    if (!m_clock) return;
    if (m_clock->isPlaying()) {
        m_clock->pause();
        m_btnPlay->setText("Play");
    } else {
        if (!m_project || m_project->trackCount() == 0
            || m_project->totalDurationTicks() == 0) return;
        m_clock->play();
        m_btnPlay->setText("Pause");
    }
}

void SequencerDock::onStopClicked()
{
    if (!m_clock) return;
    m_clock->stop();
    m_btnPlay->setText("Play");
}

void SequencerDock::onSeekSliderMoved(int value)
{
    if (!m_clock) return;
    m_clock->seek(static_cast<Tick>(value));
}

void SequencerDock::onClockTickAdvanced(Tick tick)
{
    m_seek->blockSignals(true);
    m_seek->setValue(static_cast<int>(tick));
    m_seek->blockSignals(false);

    const Tick total = m_project ? m_project->totalDurationTicks() : 0;
    m_timeLabel->setText(formatTime(tick) + " / " + formatTime(total));
}

void SequencerDock::onProjectChanged()
{
    refreshDurationUi();
    if (m_clock && !m_clock->isPlaying() && m_router) {
        m_router->refreshCurrentFrame();
    }
}

// =============================================================================
// Loop UI
// =============================================================================

void SequencerDock::onMarkIn()
{
    if (!m_clock) return;
    m_loopIn = m_clock->currentTick();
    if (m_loopOut <= m_loopIn) m_loopOut = m_project
                                           ? m_project->totalDurationTicks()
                                           : m_loopIn + secondsToTicks(5.0);
    applyLoopRegionToEngine();
}

void SequencerDock::onMarkOut()
{
    if (!m_clock) return;
    m_loopOut = m_clock->currentTick();
    if (m_loopIn >= m_loopOut) m_loopIn = 0;
    applyLoopRegionToEngine();
}

void SequencerDock::onLoopToggled(bool /*on*/)
{
    applyLoopRegionToEngine();
}

void SequencerDock::onRenderClicked()
{
    if (!m_project) return;
    if (m_project->trackCount() == 0) return;

    const bool loopRegionAvailable = m_chkLoop && m_chkLoop->isChecked()
                                     && m_loopOut > m_loopIn;

    SequencerRenderDialog dlg(m_project,
                              m_project->activeTrackIndex(),
                              loopRegionAvailable,
                              m_loopIn, m_loopOut,
                              /*defaultOutputDir=*/QString(),  // MainWindow fills this
                              this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto res = dlg.result();
    emit renderRequested(res.renderParams, res.importToProject);
}

void SequencerDock::onSpoutToggled(bool on)
{
    if (!m_spout) return;
    if (on) {
        const bool ok = m_spout->start("LaMoshPit");
        if (!ok) {
            // Keep checkbox visually truthful if init failed.
            m_chkSpout->blockSignals(true);
            m_chkSpout->setChecked(false);
            m_chkSpout->blockSignals(false);
            m_spoutStatus->setText("init failed");
            m_spoutStatus->setStyleSheet(
                "QLabel { color:#e66; font:8pt 'Segoe UI'; }");
            return;
        }
        m_spoutStatus->setText("sending as \"LaMoshPit\"");
        m_spoutStatus->setStyleSheet(
            "QLabel { color:#6c6; font:8pt 'Segoe UI'; }");
    } else {
        m_spout->stop();
        m_spoutStatus->setText("(off)");
        m_spoutStatus->setStyleSheet(
            "QLabel { color:#888; font:8pt 'Segoe UI'; }");
    }
}

void SequencerDock::applyLoopRegionToEngine()
{
    const bool on = m_chkLoop && m_chkLoop->isChecked();
    if (m_clock) m_clock->setLoopRegion(m_loopIn, m_loopOut, on);
    if (m_timelineView) m_timelineView->setLoopRegion(m_loopIn, m_loopOut, on);
}

// =============================================================================
// UI refresh
// =============================================================================

void SequencerDock::refreshDurationUi()
{
    const Tick total = m_project ? m_project->totalDurationTicks() : 0;
    m_seek->blockSignals(true);
    m_seek->setRange(0, static_cast<int>(std::min<Tick>(total,
                                                        std::numeric_limits<int>::max())));
    m_seek->blockSignals(false);

    if (m_clock) {
        m_clock->setEndTicks(total);
        if (m_project) m_clock->setOutputFrameRate(m_project->outputFrameRate());
    }

    // If loop-out hasn't been explicitly set, keep it at total so marking
    // just "in" gives a sensible default region.
    if (m_loopOut == 0 || m_loopOut > total) m_loopOut = total;
    applyLoopRegionToEngine();

    const Tick cur = m_clock ? m_clock->currentTick() : 0;
    m_timeLabel->setText(formatTime(cur) + " / " + formatTime(total));
}

QString SequencerDock::formatTime(Tick t) const
{
    if (t < 0) t = 0;
    const double seconds = ticksToSeconds(t);
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int ms   = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000.0);
    return QString("%1:%2.%3")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'))
        .arg(ms,   3, 10, QLatin1Char('0'));
}

} // namespace sequencer

#include "gui/sequencer/SequencerDock.h"
#include "gui/sequencer/SequencerPreviewPlayer.h"
#include "gui/sequencer/SequencerTimelineView.h"
#include "gui/sequencer/SequencerTrackHeader.h"
#include "gui/sequencer/SequencerTransitionPanel.h"
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
#include <QShortcut>
#include <QKeySequence>
#include <QComboBox>
#include <QApplication>
#include <QKeyEvent>
#include <QFileInfo>

namespace sequencer {

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
        "         topmost track with content at the playhead.");
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
    m_clock  = std::make_unique<SequencerPlaybackClock>(this);
    m_router = std::make_unique<FrameRouter>(m_project, m_clock.get(), this);
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
    connect(m_router.get(), &FrameRouter::frameReady,
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
    connect(m_router.get(), &FrameRouter::frameReady,
            m_spout.get(),  &SpoutSender::onFrameReady);

    // Transition panel → router.
    connect(m_transitionPanel, &SequencerTransitionPanel::typeChanged,
            m_router.get(),    &FrameRouter::setTransitionTypeId);
    connect(m_transitionPanel, &SequencerTransitionPanel::paramsChanged,
            m_router.get(),    &FrameRouter::setTransitionParams);
    // Prime router with the panel's initial values.
    m_router->setTransitionTypeId(m_transitionPanel->currentTypeId());
    m_router->setTransitionParams(m_transitionPanel->currentParams());

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

    refreshDurationUi();
}

SequencerDock::~SequencerDock()
{
    if (qApp) qApp->removeEventFilter(this);
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

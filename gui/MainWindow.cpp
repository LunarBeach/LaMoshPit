#include "MainWindow.h"
#include "widgets/TimelineWidget.h"
#include "widgets/PreviewPlayer.h"
#include "widgets/MacroblockWidget.h"
#include "widgets/GlobalParamsWidget.h"
#include "widgets/PropertyPanel.h"
#include "widgets/BitstreamTestWidget.h"
#include "widgets/QuickMoshWidget.h"
#include "widgets/MediaBinWidget.h"
#include "widgets/ProgressPanel.h"
#include "sequencer/SequencerDock.h"
#include "core/sequencer/SequencerProject.h"
#include "gui/undo/UndoController.h"
#include "gui/undo/EditorCommands.h"
#include "gui/nle_bridge/NleLauncher.h"
#include "gui/nle_bridge/NleControlChannel.h"
#include "SettingsDialog.h"
#include "gui/dialogs/ImportSelectionMapDialog.h"
#include "BitstreamAnalyzer.h"
#include "core/pipeline/DecodePipeline.h"
#include "core/transform/FrameTransformer.h"
#include "core/presets/PresetManager.h"
#include "core/presets/VersionPathUtil.h"
#include "core/project/Project.h"
#include "core/thumbnail/ThumbnailGenerator.h"
#include "core/logger/ControlLogger.h"
#include "AppFonts.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QDockWidget>
#include <QEvent>
#include <QCloseEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QThread>
#include <QFile>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QDialog>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
#include <cmath>

// =============================================================================
// Button factory helpers
// =============================================================================

static QPushButton* makeTimelineBtn(const QString& text,
                                    const QString& color,
                                    QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setFixedHeight(32);
    b->setMinimumWidth(96);
    b->setEnabled(false);
    b->setStyleSheet(
        QString("QPushButton { background:#0d0d0d; color:%1; border:1.5px solid %1; "
                "border-radius:4px; font-family:'%2','%3'; font-size:10pt; "
                "font-weight:bold; padding:0 10px; }"
                "QPushButton:hover { background:#181818; border-color:#ffffff; color:#ffffff; }"
                "QPushButton:pressed { background:#222; }"
                "QPushButton:disabled { color:#2a2a2a; border-color:#1e1e1e; }")
            .arg(color, AppFonts::displayFamily(), AppFonts::bodyFamily()));
    return b;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

MainWindow::MainWindow(QWidget *parent)
    : KDDockWidgets::QtWidgets::MainWindow(
          QStringLiteral("LaMoshPitMain"),
          KDDockWidgets::MainWindowOption_None,
          parent)
    , m_videoSequence(new VideoSequence())
{
    // KDDW's View<> template shadows QWidget::setMinimumSize(int,int) with
    // a different signature, so reach through the QMainWindow base.
    QMainWindow::setMinimumSize(1000, 640);

    // ── Project resume / auto-create ─────────────────────────────────────
    // Before any widget that reads a project path is constructed (notably
    // MediaBinWidget), ensure a Project exists.  Priority:
    //   1. Open the last-used project folder if QSettings remembers one and
    //      its manifest still parses.
    //   2. Otherwise create a fresh Untitled_{timestamp}/ folder under
    //      Documents/LaMoshPit Projects/ so the app always starts in a
    //      valid project.  File → New / Open lets the user switch later.
    {
        QSettings settings("LaMoshPit", "LaMoshPit");
        const QString lastPath = settings.value("project/lastOpened").toString();

        QString openErr;
        if (!lastPath.isEmpty())
            m_project = Project::open(lastPath, openErr);

        if (!m_project) {
            const QString root = defaultProjectsRoot();
            const QString name = QString("Untitled_%1")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
            QString createErr;
            m_project = Project::create(QDir(root).filePath(name), name, createErr);
            if (!m_project) {
                // Last-ditch: can't create in Documents, fall back to CWD.
                // App still works, bin shows whatever MoshVideoFolder/ has.
                m_project = Project::create(
                    QDir::currentPath() + "/Untitled_" +
                    QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"),
                    "Untitled", createErr);
            }
        }
        if (m_project) {
            settings.setValue("project/lastOpened", m_project->folderPath());
        }
    }

    buildLayout();

    m_seqProject = new sequencer::SequencerProject(this);
    m_seqDock    = new sequencer::SequencerDock(m_seqProject, this);
    m_seqDock->setObjectName("dock_Sequencer");
    connect(m_seqDock, &sequencer::SequencerDock::renderRequested,
            this,      &MainWindow::onNleRenderRequested);

    // Sequencer edits (add track / drop clip / trim / etc.) dirty the active
    // project so the title's "*" indicator reflects reality.  fromJson also
    // emits projectChanged() — that path calls clearDirty() after loading so
    // the spurious mark is reset to match disk state.
    connect(m_seqProject, &sequencer::SequencerProject::projectChanged,
            this, [this]() { if (m_project) m_project->markDirty(); });

    // Scope C — single application-wide undo/redo stack.  Reads the max
    // depth from Settings; every executed command marks the active project
    // dirty.  Cleared on project switch so history doesn't leak across
    // projects (see setActiveProject).
    m_undoController = std::make_unique<undo::UndoController>(this);
    m_undoController->setMaxSteps(SettingsDialog::maxUndoStepsMBEditor());
    // Push the sequencer's own cap too — NLE sequencer has an independent
    // undo stack scoped to its dock's focus; see SequencerProject and
    // SequencerDock's Ctrl+Z shortcuts.
    if (m_seqProject)
        m_seqProject->setMaxUndoSteps(SettingsDialog::maxUndoStepsSequencer());
    connect(m_undoController.get(), &undo::UndoController::commandExecuted,
            this, [this]() {
                if (m_project) m_project->markDirty();
                // Any command execution (redo/undo/execute) replaces widget
                // state with a known snapshot — update the cache so the next
                // diff uses the correct baseline.
                syncLastKnownToWidgets();
            });

    // Debounce timers for Scope C MB / GP edit commits.  Single-shot; the
    // onEdit slot start()s them (which restarts if already running), so a
    // rapid knob drag emits many editCommitted signals but the debouncer
    // fires once at the end.
    m_mbCommitTimer = new QTimer(this);
    m_mbCommitTimer->setSingleShot(true);
    m_mbCommitTimer->setInterval(200);
    connect(m_mbCommitTimer, &QTimer::timeout,
            this, &MainWindow::commitMBEditIfChanged);

    m_gpCommitTimer = new QTimer(this);
    m_gpCommitTimer->setSingleShot(true);
    m_gpCommitTimer->setInterval(200);
    connect(m_gpCommitTimer, &QTimer::timeout,
            this, &MainWindow::commitGPIfChanged);

    if (m_mbWidget) {
        connect(m_mbWidget, &MacroblockWidget::editCommitted,
                this, &MainWindow::onMBEditCommitted);
    }
    if (m_globalParams) {
        connect(m_globalParams, &GlobalParamsWidget::paramsChanged,
                this, &MainWindow::onGPParamsChanged);
    }

    applyDefaultLayout();
    {
        QSettings settings("LaMoshPit", "LaMoshPit");
        const QByteArray savedGeom   = settings.value("layout/lastGeometry").toByteArray();
        const QByteArray savedLayout = settings.value("layout/kddwLayout").toByteArray();
        if (!savedGeom.isEmpty()) restoreGeometry(savedGeom);
        if (!savedLayout.isEmpty()) {
            KDDockWidgets::LayoutSaver saver;
            saver.restoreLayout(savedLayout);
        }
    }

    buildMenuBar();

    // ── Two-process NLE bridge (Phase 1 Step 3) ──────────────────────────
    // Spin up the IPC server, spawn LaMoshPit_NLE.exe, supervise its
    // lifetime via a Windows Job Object so Task-Manager-killing us also
    // kills the NLE (no orphan processes).  If the NLE exe isn't present
    // (developer hasn't run `bash scripts/build-nle-msys2.sh` yet), we
    // just log a warning and carry on — LaMoshPit.exe still works, just
    // without the NLE sequencer window.
    m_nleControl  = std::make_unique<lamosh::NleControlChannel>(this);
    m_nleLauncher = std::make_unique<lamosh::NleLauncher>(this);

    connect(m_nleControl.get(), &lamosh::NleControlChannel::nleConnected,
            this, [this]() {
        statusBar()->showMessage(
            "LaMoshPit — NLE sequencer connected", 3000);
    });
    connect(m_nleControl.get(), &lamosh::NleControlChannel::nleDisconnected,
            this, [this]() {
        statusBar()->showMessage(
            "LaMoshPit — NLE sequencer disconnected", 3000);
    });
    connect(m_nleControl.get(), &lamosh::NleControlChannel::eventReceived,
            this, [this](const QJsonObject& evt) {
        const QString kind = evt.value(QStringLiteral("evt")).toString();
        if (kind == QLatin1String("ready")) {
            const QString ver = evt.value(QStringLiteral("version")).toString();
            qInfo() << "[nle] ready handshake received, version=" << ver;
        }
        // Other events dispatched as Step 4+ features land.
    });
    connect(m_nleLauncher.get(), &lamosh::NleLauncher::nleExited,
            this, [this](int exitCode, bool normal) {
        if (!normal) {
            statusBar()->showMessage(
                QString("NLE sequencer exited unexpectedly (code %1)")
                    .arg(exitCode), 5000);
        }
    });

    {
        const QString pipeName = m_nleControl->listen();
        if (pipeName.isEmpty()) {
            qWarning() << "[nle] could not start IPC server; NLE will not launch";
        } else if (!m_nleLauncher->launch(pipeName)) {
            qWarning() << "[nle] launch failed; continuing without NLE";
        }
    }

    updateWindowTitle();
    statusBar()->showMessage("LaMoshPit — Ready for chaos");
}

MainWindow::~MainWindow()
{
    delete m_videoSequence;
}

// =============================================================================
// buildLayout — QDockWidget-based layout.
//
// Central widget: the clip timeline strip (always present).
// Every other panel is a QDockWidget — users can float, tab-group, or close
// them. Layout state is saved/restored via QMainWindow::saveState() + a
// per-name QSettings entry (see onSaveLayoutAs / onLoadLayout).
//
// Default placement (applyDefaultLayout):
//   ┌──────────────────────────────────────────────────────────────────┐
//   │ Preview (L) │ Timeline central │ MB Editor / Sequencer tabs (R) │
//   ├──────────────────────────────────────────────────────────────────┤
//   │ Media Bin │ Quick Mosh │ Global Encode Params │ (Debug Tools)    │
//   └──────────────────────────────────────────────────────────────────┘
// =============================================================================

KDDockWidgets::QtWidgets::DockWidget*
MainWindow::wrapInDock(const QString& title, QWidget* widget,
                       const QString& uniqueName, int dockOptions)
{
    // KDDW uses uniqueName (not parent-based objectName) as the key for
    // LayoutSaver persistence. Title is what appears in the dock tab/strip.
    auto* dock = new KDDockWidgets::QtWidgets::DockWidget(
        uniqueName, KDDockWidgets::DockWidgetOptions(dockOptions));
    dock->setTitle(title);
    dock->setWidget(widget);
    return dock;
}

void MainWindow::buildLayout()
{
    // ── Central widget: clip timeline + frame-action buttons ────────────────
    auto* timelinePanel = new QWidget(this);
    timelinePanel->setObjectName("TimelinePanel");
    timelinePanel->setStyleSheet(
        "#TimelinePanel { background:#080808; border-top:1px solid #1e1e1e; "
        "                 border-bottom:1px solid #1e1e1e; }");
    timelinePanel->setMinimumHeight(140);

    auto* tlLayout = new QVBoxLayout(timelinePanel);
    tlLayout->setContentsMargins(0, 0, 0, 0);
    tlLayout->setSpacing(0);

    // Timeline widget itself
    m_timeline = new TimelineWidget(timelinePanel);
    tlLayout->addWidget(m_timeline, 1);

    // Control button row beneath the timeline
    auto* ctrlRow = new QWidget(timelinePanel);
    ctrlRow->setFixedHeight(38);
    ctrlRow->setStyleSheet("background:#060606; border-top:1px solid #1a1a1a;");
    auto* ctrlLayout = new QHBoxLayout(ctrlRow);
    ctrlLayout->setContentsMargins(8, 4, 8, 4);
    ctrlLayout->setSpacing(5);

    m_selectionLabel = new QLabel("No frames selected", ctrlRow);
    m_selectionLabel->setStyleSheet(
        "QLabel { color:#3a3a3a; font:8pt 'Consolas'; background:transparent; }");
    m_selectionLabel->setMinimumWidth(140);
    ctrlLayout->addWidget(m_selectionLabel);
    ctrlLayout->addSpacing(8);

    // Frame type buttons — colour-coded to match timeline badges
    m_btnForceI   = makeTimelineBtn("Force \u2192 I", "#ffffff", ctrlRow);
    m_btnForceP   = makeTimelineBtn("Force \u2192 P", "#4488ff", ctrlRow);
    m_btnForceB   = makeTimelineBtn("Force \u2192 B", "#ff64b4", ctrlRow);
    m_btnDelete   = makeTimelineBtn("\u2716 Delete",   "#ff3333", ctrlRow);
    m_btnDupLeft   = makeTimelineBtn("\u276E Dup",         "#00ff88", ctrlRow);
    m_btnDupRight  = makeTimelineBtn("Dup \u276F",         "#00ff88", ctrlRow);
    m_btnInterpLeft  = makeTimelineBtn("\u2190 INTERPOLATE", "#ff9900", ctrlRow);
    m_btnInterpRight = makeTimelineBtn("INTERPOLATE \u2192", "#ff9900", ctrlRow);
    m_btnFlip  = makeTimelineBtn("Flip",  "#cc88ff", ctrlRow);
    m_btnFlop  = makeTimelineBtn("Flop",  "#cc88ff", ctrlRow);
    m_btnFlip->setToolTip("Mirror selected frames vertically (upside-down)");
    m_btnFlop->setToolTip("Mirror selected frames horizontally (left-right)");

    // Undo button removed — replaced by non-destructive renders with Media Bin
    // iteration history.  Ctrl+Z now walks VersionPathUtil::previousVersionPath
    // to load the nearest older iteration.  See onLoadPreviousVersion + the
    // QShortcut binding below in the constructor.

    ctrlLayout->addWidget(m_btnForceI);
    ctrlLayout->addWidget(m_btnForceP);
    ctrlLayout->addWidget(m_btnForceB);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(m_btnDupLeft);
    ctrlLayout->addWidget(m_btnDupRight);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(m_btnInterpLeft);
    ctrlLayout->addWidget(m_btnInterpRight);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(m_btnFlip);
    ctrlLayout->addWidget(m_btnFlop);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(m_btnDelete);

    ctrlLayout->addStretch(1);

    tlLayout->addWidget(ctrlRow);

    m_timelineCentral = timelinePanel;
    // Regular closable dock — user can hide it from the View menu like any
    // other panel, or drag it to a second monitor.
    m_timelineDock = wrapInDock("Timeline", timelinePanel, "dock_Timeline");

    // ── Construct panel widgets ─────────────────────────────────────────────
    m_preview  = new PreviewPlayer(nullptr);
    m_mbWidget = new MacroblockWidget(nullptr);

    const QString moshVideoFolder = m_project
        ? m_project->moshVideoFolder()
        : QDir::currentPath() + "/MoshVideoFolder";
    const QString thumbnailsDir = m_project
        ? m_project->thumbnailsDir()
        : QString();
    m_mediaBin     = new MediaBinWidget(moshVideoFolder, thumbnailsDir, nullptr);
    m_quickMosh    = new QuickMoshWidget(nullptr);
    m_globalParams = new GlobalParamsWidget(nullptr);
    m_progressPanel = new ProgressPanel(nullptr);
    m_propertyPanel = new PropertyPanel(nullptr);
    m_bitstreamTest = new BitstreamTestWidget(nullptr);

    // ── Wrap each panel in a QDockWidget ────────────────────────────────────
    // MacroblockWidget exposes two sibling view widgets (canvas + controls)
    // but retains single ownership of state / signals / slots, so splitting
    // them across two docks changes zero behaviour.
    m_mbWidget->setParent(this);
    m_previewDock       = wrapInDock("Preview Player",      m_preview,                 "dock_Preview");
    m_mbCanvasDock      = wrapInDock("MB Grid Canvas",      m_mbWidget->canvasPanel(), "dock_MBCanvas");
    m_mbEditorDock      = wrapInDock("MB Editor",           m_mbWidget->controlsPanel(),"dock_MBEditor");
    m_mediaBinDock      = wrapInDock("Media Bin",           m_mediaBin,                "dock_MediaBin");
    m_quickMoshDock     = wrapInDock("Quick Mosh",          m_quickMosh,               "dock_QuickMosh");
    m_globalParamsDock  = wrapInDock("Global Encode Params",m_globalParams,            "dock_GlobalParams");
    m_progressDock      = wrapInDock("Progress",            m_progressPanel,           "dock_Progress");
    m_propertyPanelDock = wrapInDock("Properties",          m_propertyPanel,           "dock_Properties");
    m_bitstreamTestDock = wrapInDock("Bitstream Debug",     m_bitstreamTest,           "dock_BitstreamTest");

    // Pop-out button on PreviewPlayer now toggles its dock's float state.
    connect(m_preview, &PreviewPlayer::popOutRequested, this, [this]() {
        m_previewDock->setFloating(!m_previewDock->isFloating());
    });

    // Pop-out button inside the MB canvas nav bar floats the canvas dock.
    // MB Canvas dock's topLevelChanged updates the button icon so it stays
    // in sync with reality (user can also float via the dock title bar).
    connect(m_mbWidget, &MacroblockWidget::canvasFloatToggleRequested, this, [this]() {
        m_mbCanvasDock->setFloating(!m_mbCanvasDock->isFloating());
    });
    connect(m_mbCanvasDock,
            &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged,
            m_mbWidget, &MacroblockWidget::setCanvasFloatingIcon);

    // MediaBin signals — same routing as before.
    connect(m_mediaBin, &MediaBinWidget::videoSelected,
            this, &MainWindow::onMediaBinVideoSelected);
    connect(m_mediaBin, &MediaBinWidget::importRequested,
            this, &MainWindow::openFile);

    // ── Signal connections ────────────────────────────────────────────────────
    connect(m_timeline, &TimelineWidget::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    connect(m_timeline, &TimelineWidget::selectionChanged,
            m_mbWidget, &MacroblockWidget::setActiveFrameRange);

    connect(m_mbWidget, &MacroblockWidget::frameNavigated,
            this, &MainWindow::onMBFrameNavigated);

    connect(m_globalParams, &GlobalParamsWidget::applyRequested,
            this, &MainWindow::onApplyGlobalParams);

    connect(m_quickMosh, &QuickMoshWidget::saveUserPresetRequested,
            this, &MainWindow::onQuickMoshSaveUserPreset);
    connect(m_quickMosh, &QuickMoshWidget::userMoshRequested,
            this, &MainWindow::onQuickMoshUserMosh);

    connect(m_btnForceI,     &QPushButton::clicked, this, &MainWindow::onForceI);
    connect(m_btnForceP,     &QPushButton::clicked, this, &MainWindow::onForceP);
    connect(m_btnForceB,     &QPushButton::clicked, this, &MainWindow::onForceB);
    connect(m_btnDelete,     &QPushButton::clicked, this, &MainWindow::onDeleteFrames);
    connect(m_btnDupLeft,    &QPushButton::clicked, this, &MainWindow::onDupLeft);
    connect(m_btnDupRight,   &QPushButton::clicked, this, &MainWindow::onDupRight);
    connect(m_btnInterpLeft,  &QPushButton::clicked, this, &MainWindow::onInterpLeft);
    connect(m_btnInterpRight, &QPushButton::clicked, this, &MainWindow::onInterpRight);
    connect(m_btnFlip,       &QPushButton::clicked, this, &MainWindow::onFlip);
    connect(m_btnFlop,       &QPushButton::clicked, this, &MainWindow::onFlop);

    connect(m_timeline, &TimelineWidget::frameReorderRequested,
            this, &MainWindow::onFrameReorderRequested);

    // Ctrl+Z / Ctrl+Shift+Z are now the unified application undo/redo
    // (Scope C).  The Edit menu actions carry the shortcuts — see
    // buildMenuBar.  "Load previous render version" is now a menu-only
    // action (no shortcut).
}

// =============================================================================
// buildMenuBar
// =============================================================================

void MainWindow::buildMenuBar()
{
    // File menu
    auto* fileMenu = menuBar()->addMenu("&File");

    // Project lifecycle.  A project is a folder on disk containing all
    // imports, render iterations, thumbnails, and logs — see core/project.
    // Exactly one project is always active; File → New / Open switches.
    fileMenu->addAction("&New Project...",  this, &MainWindow::onNewProject,
                        QKeySequence("Ctrl+Shift+N"));
    fileMenu->addAction("&Open Project...", this, &MainWindow::onOpenProject,
                        QKeySequence("Ctrl+Shift+O"));
    fileMenu->addAction("&Save Project",    this, &MainWindow::onSaveProject,
                        QKeySequence("Ctrl+Shift+S"));
    fileMenu->addSeparator();
    // Multi-file import.  Each selected file is transcoded via DecodePipeline
    // and lands in the active project's MoshVideoFolder/ subdir.
    fileMenu->addAction("&Import Videos...", this, &MainWindow::openFile,
                        QKeySequence("Ctrl+O"));
    fileMenu->addAction("Import Selection &Map...", this,
                        &MainWindow::onImportSelectionMap);
    fileMenu->addAction("&Save Mosh Pit...", this, &MainWindow::saveHacked,
                        QKeySequence("Ctrl+S"));

    // Application-wide preferences.
    fileMenu->addSeparator();
    fileMenu->addAction("S&ettings...", this, [this]() {
        SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            // Pick up any UI-preference changes (selection overlay hue,
            // max undo depths for both workspaces).
            if (m_mbWidget) m_mbWidget->refreshSelectionColor();
            if (m_undoController)
                m_undoController->setMaxSteps(SettingsDialog::maxUndoStepsMBEditor());
            if (m_seqProject)
                m_seqProject->setMaxUndoSteps(SettingsDialog::maxUndoStepsSequencer());
        }
    }, QKeySequence("Ctrl+,"));

    // Edit menu — Scope C unified undo/redo.  The controller owns the
    // stack; actions here are thin wrappers that call undo()/redo() and
    // flip enabled-state on stateChanged.  "Load Previous Render Version"
    // was formerly on Ctrl+Z (replaced by undo) and is now menu-only —
    // users walking the render-iteration ladder typically do so via the
    // Media Bin anyway.
    auto* editMenu = menuBar()->addMenu("&Edit");
    // The lambdas wrap undo/redo to first flush any pending debounced
    // commit — otherwise a quick Ctrl+Z right after a knob turn might
    // see nothing on the stack because the 200 ms debounce hasn't fired.
    QAction* undoAct = editMenu->addAction("&Undo", this, [this]() {
        flushPendingCommits();
        m_undoController->undo();
    }, QKeySequence::Undo);
    QAction* redoAct = editMenu->addAction("&Redo", this, [this]() {
        flushPendingCommits();
        m_undoController->redo();
    });
    // Bind both Ctrl+Shift+Z (standard on Linux/Mac, user-requested) and
    // Ctrl+Y (Windows convention) so either habit works.
    redoAct->setShortcuts({ QKeySequence("Ctrl+Shift+Z"),
                            QKeySequence("Ctrl+Y") });
    undoAct->setEnabled(m_undoController->canUndo());
    redoAct->setEnabled(m_undoController->canRedo());
    connect(m_undoController.get(), &undo::UndoController::stateChanged,
            this, [this, undoAct, redoAct]() {
                undoAct->setEnabled(m_undoController->canUndo());
                redoAct->setEnabled(m_undoController->canRedo());
            });
    editMenu->addSeparator();
    editMenu->addAction("Load Previous Render Version", this,
                        &MainWindow::onLoadPreviousVersion);

    // View menu — KDDW docks expose toggleAction() (auto-synced with dock
    // open/close state, floating, tab-grouping). The NLE sequencer is still
    // a QDockWidget so it uses toggleViewAction().
    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_timelineDock     ->toggleAction());
    viewMenu->addAction(m_previewDock      ->toggleAction());
    viewMenu->addAction(m_mbCanvasDock     ->toggleAction());
    viewMenu->addAction(m_mbEditorDock     ->toggleAction());
    viewMenu->addSeparator();
    viewMenu->addAction(m_mediaBinDock     ->toggleAction());
    viewMenu->addAction(m_quickMoshDock    ->toggleAction());
    viewMenu->addAction(m_globalParamsDock ->toggleAction());
    viewMenu->addAction(m_progressDock     ->toggleAction());
    viewMenu->addSeparator();
    if (m_seqDock) {
        QAction* seqAct = m_seqDock->toggleViewAction();
        seqAct->setText("NLE Sequencer");
        viewMenu->addAction(seqAct);
    }
    viewMenu->addAction(m_propertyPanelDock->toggleAction());
    viewMenu->addSeparator();

    // Debug tools — BitstreamTest dock, hidden by default.
    m_actDebugTools = m_bitstreamTestDock->toggleAction();
    m_actDebugTools->setText("Debug Tools");
    viewMenu->addAction(m_actDebugTools);

    // ── Layout menu ─────────────────────────────────────────────────────────
    // Save/load named layouts via QSettings. Each named layout stores the
    // full QMainWindow state (dock positions, sizes, floating, tabs) and
    // window geometry. Reset restores the compiled-in default.
    m_layoutMenu = menuBar()->addMenu("&Layout");
    m_layoutMenu->addAction("&Save Layout As...", this,
                            &MainWindow::onSaveLayoutAs,
                            QKeySequence("Ctrl+Shift+L"));
    m_loadLayoutMenu = m_layoutMenu->addMenu("&Load Layout");
    m_layoutMenu->addSeparator();
    m_layoutMenu->addAction("&Reset to Default", this,
                            &MainWindow::onResetLayout);
    rebuildLayoutMenu();
}

// =============================================================================
// Default layout
// =============================================================================

void MainWindow::applyDefaultLayout()
{
    using KDDockWidgets::Location_OnLeft;
    using KDDockWidgets::Location_OnRight;
    using KDDockWidgets::Location_OnBottom;
    using KDDockWidgets::Location_OnTop;

    // Timeline is the anchor other docks reference. Preview on the left of
    // it, MB canvas + editor to the right. Bottom row: media bin / quick
    // mosh / global params.
    addDockWidget(m_timelineDock,     Location_OnTop);
    addDockWidget(m_previewDock,      Location_OnLeft,  m_timelineDock);
    addDockWidget(m_mbCanvasDock,     Location_OnRight, m_timelineDock);
    addDockWidget(m_mbEditorDock,     Location_OnRight, m_mbCanvasDock);
    addDockWidget(m_mediaBinDock,     Location_OnBottom);
    addDockWidget(m_quickMoshDock,    Location_OnRight, m_mediaBinDock);
    addDockWidget(m_globalParamsDock, Location_OnRight, m_quickMoshDock);
    addDockWidget(m_progressDock,     Location_OnTop,   m_quickMoshDock);

    // SequencerDock is a QDockWidget subclass and can't participate in the
    // KDDW layout. Kept as a standalone floating window, shown via View menu.
    // All its hotkey / render / timeline behavior is preserved.
    if (m_seqDock) {
        m_seqDock->setFloating(true);
        m_seqDock->hide();
    }

    // Stubs / debug — not docked on first launch; View menu re-opens them.
    m_bitstreamTestDock->close();
    m_propertyPanelDock->close();
}

// =============================================================================
// Layout menu — save/load named layouts
// =============================================================================

void MainWindow::onSaveLayoutAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Layout",
        "Name for this layout:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    KDDockWidgets::LayoutSaver saver;
    const QByteArray layout = saver.serializeLayout();

    QSettings settings("LaMoshPit", "LaMoshPit");
    settings.setValue(QString("layouts/%1/kddw").arg(name),     layout);
    settings.setValue(QString("layouts/%1/geometry").arg(name), saveGeometry());

    QStringList names = settings.value("layouts/index").toStringList();
    if (!names.contains(name)) {
        names << name;
        settings.setValue("layouts/index", names);
    }
    rebuildLayoutMenu();
    statusBar()->showMessage(QString("Saved layout \"%1\"").arg(name), 3000);
}

void MainWindow::onLoadLayout(const QString& name)
{
    QSettings settings("LaMoshPit", "LaMoshPit");
    const QByteArray layout = settings.value(QString("layouts/%1/kddw").arg(name)).toByteArray();
    const QByteArray geom   = settings.value(QString("layouts/%1/geometry").arg(name)).toByteArray();
    if (layout.isEmpty()) {
        QMessageBox::warning(this, "Load Layout",
            QString("Layout \"%1\" not found.").arg(name));
        return;
    }
    if (!geom.isEmpty()) restoreGeometry(geom);
    KDDockWidgets::LayoutSaver saver;
    saver.restoreLayout(layout);
    statusBar()->showMessage(QString("Loaded layout \"%1\"").arg(name), 3000);
}

void MainWindow::onResetLayout()
{
    // Close every dock, then replay applyDefaultLayout. KDDW handles
    // tearing down nested splits / floats cleanly when a dock is closed.
    for (KDDW_Dock* d : { m_timelineDock, m_previewDock, m_mbCanvasDock,
                          m_mbEditorDock, m_mediaBinDock, m_quickMoshDock,
                          m_globalParamsDock, m_progressDock,
                          m_propertyPanelDock, m_bitstreamTestDock }) {
        if (d) d->close();
    }
    if (m_seqDock) {
        m_seqDock->hide();
    }
    applyDefaultLayout();
    m_timelineDock->open();
    m_previewDock->open();
    m_mbCanvasDock->open();
    m_mbEditorDock->open();
    m_mediaBinDock->open();
    m_quickMoshDock->open();
    m_globalParamsDock->open();
    m_progressDock->open();
    statusBar()->showMessage("Reset to default layout", 3000);
}

void MainWindow::rebuildLayoutMenu()
{
    if (!m_loadLayoutMenu) return;
    m_loadLayoutMenu->clear();

    QSettings settings("LaMoshPit", "LaMoshPit");
    const QStringList names = settings.value("layouts/index").toStringList();
    if (names.isEmpty()) {
        QAction* empty = m_loadLayoutMenu->addAction("(no saved layouts)");
        empty->setEnabled(false);
        return;
    }
    for (const QString& name : names) {
        m_loadLayoutMenu->addAction(name, this, [this, name]() {
            onLoadLayout(name);
        });
    }
    m_loadLayoutMenu->addSeparator();
    m_loadLayoutMenu->addAction("Manage...", this, [this]() {
        QSettings s("LaMoshPit", "LaMoshPit");
        QStringList names = s.value("layouts/index").toStringList();
        if (names.isEmpty()) return;
        bool ok = false;
        const QString toDelete = QInputDialog::getItem(this, "Delete Layout",
            "Select a layout to delete:", names, 0, false, &ok);
        if (!ok || toDelete.isEmpty()) return;
        s.remove(QString("layouts/%1").arg(toDelete));
        names.removeAll(toDelete);
        s.setValue("layouts/index", names);
        rebuildLayoutMenu();
    });
}

// =============================================================================
// File open
// =============================================================================

void MainWindow::openFile()
{
    if (!m_project) {
        QMessageBox::warning(this, "No Project",
            "No active project — cannot import videos.");
        return;
    }

    // Batch import: user can select 1 or N files; we process them serially so
    // a single splash + progress bar drives the whole operation.  Each file
    // transcodes through DecodePipeline, lands in the project's
    // MoshVideoFolder/, and then gets a thumbnail generated before we move
    // on.  The LAST file to finish becomes the active video.
    const QStringList fileList = QFileDialog::getOpenFileNames(this,
        "Import Video(s)", "",
        "Video Files (*.mp4 *.mov *.mkv *.avi *.264 *.h264);;All Files (*.*)");

    if (fileList.isEmpty()) return;

    QDir importDir(m_project->moshVideoFolder());
    if (!importDir.exists()) importDir.mkpath(".");

    // ── Splash dialog ─────────────────────────────────────────────────────────
    auto* splash = new QDialog(this, Qt::FramelessWindowHint | Qt::Dialog);
    splash->setFixedSize(560, 340);
    splash->setAttribute(Qt::WA_DeleteOnClose);
    splash->setStyleSheet("background:#0a0a0a; border:2px solid #00ff88; border-radius:8px;");

    auto* sLayout = new QVBoxLayout(splash);
    sLayout->setContentsMargins(20, 20, 20, 20);
    sLayout->setSpacing(12);

    // Background image
    auto* bgLabel = new QLabel(splash);
    QPixmap bg(":/assets/png/Open_Video_Splash_Background.png");
    if (!bg.isNull()) {
        bgLabel->setPixmap(bg.scaled(splash->size(), Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation));
        bgLabel->setGeometry(0, 0, splash->width(), splash->height());
        bgLabel->lower();
    }

    sLayout->addStretch(1);

    // Header text with pulsing glow — Nodo for dramatic display-size text.
    auto* headerLbl = new QLabel("You might think you're ready...\nbut you're not.", splash);
    headerLbl->setAlignment(Qt::AlignCenter);
    headerLbl->setFont(AppFonts::display(22));
    headerLbl->setStyleSheet(
        "color:#00ff88; background:transparent; border:none;");
    auto* glow = new QGraphicsDropShadowEffect(headerLbl);
    glow->setColor(QColor(0x00, 0xff, 0x88, 180));
    glow->setBlurRadius(20);
    glow->setOffset(0, 0);
    headerLbl->setGraphicsEffect(glow);
    sLayout->addWidget(headerLbl);

    // Pulse the glow.  IMPORTANT: pulsePhase is captured by VALUE into a
    // mutable lambda (was previously captured by reference to a stack-local
    // that died as soon as openFile() returned — a pre-existing UB that
    // single-file imports didn't live long enough to trip, but the longer
    // batch flow reliably did).
    auto* pulseTimer = new QTimer(splash);
    connect(pulseTimer, &QTimer::timeout, splash,
        [glow, pulsePhase = 0]() mutable {
        pulsePhase = (pulsePhase + 6) % 360;
        double t = (1.0 + sin(pulsePhase * 3.14159265 / 180.0)) / 2.0;
        int alpha = 80 + (int)(175 * t);
        int blur  = 12 + (int)(18 * t);
        glow->setColor(QColor(0x00, 0xff, 0x88, alpha));
        glow->setBlurRadius(blur);
    });
    pulseTimer->start(40);

    // Progress bar (determinate — updated by worker thread via shared atomics)
    auto* progressBar = new QProgressBar(splash);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setFixedHeight(14);
    progressBar->setTextVisible(false);
    progressBar->setStyleSheet(
        "QProgressBar { background:#111; border:1px solid #00ff88; border-radius:6px; }"
        "QProgressBar::chunk { background:qlineargradient("
        "x1:0,y1:0,x2:1,y2:0, stop:0 #003311, stop:0.5 #00ff88, stop:1 #003311); "
        "border-radius:5px; }");
    sLayout->addWidget(progressBar);

    // Percentage label
    auto* pctLabel = new QLabel("0%", splash);
    pctLabel->setAlignment(Qt::AlignCenter);
    pctLabel->setStyleSheet(
        "color:#00ff88; font:bold 9pt 'Consolas'; background:transparent; border:none;");
    sLayout->addWidget(pctLabel);

    // Status text
    auto* statusLbl = new QLabel(splash);
    statusLbl->setAlignment(Qt::AlignCenter);
    statusLbl->setWordWrap(true);
    statusLbl->setStyleSheet(
        "color:#00ff88; font:8pt 'Consolas'; background:#0a0a0a; border:none; "
        "padding:4px 8px; border-radius:4px;");
    statusLbl->setText(QString("Creating mosh pit proxy file in %1").arg(
        QDir::toNativeSeparators(importDir.absolutePath())));
    sLayout->addWidget(statusLbl);

    sLayout->addStretch(1);

    splash->show();
    splash->raise();

    // ── Shared progress state (written by worker, read by UI timer) ──────────
    auto progressCurrent = std::make_shared<std::atomic<int>>(0);
    auto progressTotal   = std::make_shared<std::atomic<int>>(0);

    auto* progressTimer = new QTimer(splash);
    connect(progressTimer, &QTimer::timeout, splash,
            [progressBar, pctLabel, progressCurrent, progressTotal]() {
        int cur = progressCurrent->load();
        int tot = progressTotal->load();
        if (tot > 0) {
            int pct = qBound(0, (int)((qint64)cur * 100 / tot), 100);
            progressBar->setValue(pct);
            pctLabel->setText(QString::number(pct) + "%");
        }
    });
    progressTimer->start(60);

    // ── Serial batch import state ────────────────────────────────────────────
    // Heap-allocated so the chain of async callbacks can all see the same
    // queue + progress state; torn down in the last finished() callback.
    struct BatchCtx {
        QStringList  remaining;           // absolute input paths still to do
        int          total;               // total file count for "[N/T]" display
        int          currentIdx;          // 1-based index of the file now running
        QString      lastOutputPath;      // set on each success; drives final load
        int          successCount;
        int          failCount;
        QStringList  failedNames;
    };
    auto* batch = new BatchCtx;
    batch->remaining   = fileList;
    batch->total       = fileList.size();
    batch->currentIdx  = 0;
    batch->successCount = 0;
    batch->failCount   = 0;

    // Mutable helper so the completion callback can re-invoke itself for the
    // next file in the queue.  std::function wrapper gives the lambda access
    // to its own name via the captured pointer.
    auto* startOne = new std::function<void()>;

    *startOne = [this, splash, batch, startOne, progressBar, pctLabel,
                 statusLbl, progressCurrent, progressTotal, importDir]()
    {
        if (batch->remaining.isEmpty()) {
            // All files processed — close splash, load the last one, report.
            splash->close();

            if (!batch->lastOutputPath.isEmpty()) {
                const QString newPath = batch->lastOutputPath;
                const QString name = QFileInfo(newPath).fileName();
                statusBar()->showMessage(
                    QString("Imported %1 file%2 \u2014 loading %3 ...")
                        .arg(batch->successCount)
                        .arg(batch->successCount == 1 ? "" : "s")
                        .arg(name));
                switchActiveClip(newPath);
                if (m_project) {
                    m_project->setActiveVideo(m_currentVideoPath);
                    QString saveErr; m_project->save(saveErr);
                }
                if (m_mediaBin) {
                    m_mediaBin->refresh();
                    m_mediaBin->setCurrentVideoPath(m_currentVideoPath);
                }
            }
            if (batch->failCount > 0) {
                QMessageBox::warning(this, "Some Imports Failed",
                    QString("Imported %1 file(s), failed %2:\n\n%3")
                        .arg(batch->successCount).arg(batch->failCount)
                        .arg(batch->failedNames.join('\n')));
            }

            // Defer the self-destruct to a fresh stack.  Doing `delete
            // startOne; delete batch;` inline here is a use-after-free in
            // debug builds: `delete startOne` frees the std::function we're
            // currently inside, destroying its capture storage, and the
            // very next line re-reads `batch` from those captures.  The
            // release build's register caching usually masked it, but
            // debug compiled with no caching reliably crashed.
            //
            // Fix: copy the two pointers into the timer lambda's own
            // captures (which are independent of startOne's captures) and
            // delete them there, by which point startOne's body has fully
            // returned and no captures are being read anymore.
            QTimer::singleShot(0, [startOne, batch]() {
                delete startOne;
                delete batch;
            });
            return;
        }

        // Pull next file, reset progress, update splash status.
        const QString fileName   = batch->remaining.takeFirst();
        batch->currentIdx++;
        const QString baseName   = QFileInfo(fileName).completeBaseName();
        const QString outputPath = importDir.absoluteFilePath(baseName + "_imported.mp4");

        progressBar->setValue(0);
        pctLabel->setText("0%");
        progressCurrent->store(0);
        progressTotal->store(0);
        statusLbl->setText(QString("[%1/%2] Transcoding: %3")
            .arg(batch->currentIdx).arg(batch->total).arg(baseName));

        auto* watcher = new QFutureWatcher<bool>(splash);
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, splash,
                [this, batch, startOne, watcher,
                 outputPath, baseName, progressBar, pctLabel]()
        {
            const bool ok = watcher->result();
            progressBar->setValue(100);
            pctLabel->setText("100%");

            if (ok) {
                batch->successCount++;
                batch->lastOutputPath = outputPath;
                // Generate thumbnail for this import (best-effort).
                if (m_project) {
                    QString thumbErr;
                    ThumbnailGenerator::generateMidFrameThumbnail(
                        outputPath,
                        m_project->thumbnailPathFor(outputPath),
                        thumbErr);
                }
                // Refresh bin incrementally so the user sees imports appearing
                // as they complete, not all at once at the end.
                if (m_mediaBin) m_mediaBin->refresh();
            } else {
                batch->failCount++;
                batch->failedNames << baseName;
            }
            watcher->deleteLater();

            // Defer the next iteration (or the final "all done" finalizer) to
            // the next event-loop tick via a zero-delay single-shot.  Without
            // this, the finalizer for the LAST file runs while we're still
            // inside the watcher's finished() slot — and the heavy work it
            // does (splash->close(), m_preview->loadVideo(), analyze, bin
            // refresh) can spin a nested event loop that processes deferred
            // deletes, tearing this watcher down from under us.  Files have
            // already landed on disk by this point, which is why the next
            // session shows both videos even though the app crashed.
            //
            // startOne is a heap-allocated std::function<void()>; the lambda
            // captures the raw pointer so it can keep invoking itself.  By
            // the time this timer fires, the watcher's slot has fully
            // returned and we're safe to do splash close + video load.
            QTimer::singleShot(0, this, [startOne]() { (*startOne)(); });
        });

        // Read the HW-on-import preference once per import so the user
        // can flip it in Settings between files and have the next import
        // honour the new choice.
        const bool useHwEncode = SettingsDialog::importUsesHwEncode();
        watcher->setFuture(QtConcurrent::run(
                [fileName, outputPath, progressCurrent, progressTotal, useHwEncode]() {
            return DecodePipeline::standardizeVideo(fileName, outputPath,
                [progressCurrent, progressTotal](int cur, int tot) {
                    progressCurrent->store(cur);
                    progressTotal->store(tot);
                },
                useHwEncode);
        }));
    };

    (*startOne)();  // kick off file 1
}

// =============================================================================
// Analysis → timeline population
// =============================================================================

void MainWindow::analyzeImportedVideo(const QString& videoPath)
{
    // Breadcrumb trace — mirrors reloadVideoAndTimeline so a crash during
    // a fresh import is traceable in the same log format.
    auto& L = ControlLogger::instance();
    L.logNote("analyzeImportedVideo — step A: BitstreamAnalyzer::analyzeVideo()");
    m_lastAnalysis = BitstreamAnalyzer::analyzeVideo(videoPath);
    L.logNote(QString("analyzeImportedVideo — step B: analysis.success = %1")
              .arg(m_lastAnalysis.success ? "true" : "false"));

    if (m_lastAnalysis.success) {
        L.logNote("analyzeImportedVideo — step C: printAnalysis");
        BitstreamAnalyzer::printAnalysis(m_lastAnalysis);
        L.logNote("analyzeImportedVideo — step D: saveAnalysisToFile");
        BitstreamAnalyzer::saveAnalysisToFile(m_lastAnalysis, videoPath);
        L.logNote("analyzeImportedVideo — step E: populateTimeline");
        populateTimeline(m_lastAnalysis);
        L.logNote("analyzeImportedVideo — step F: mbWidget->setVideo()");
        m_mbWidget->setVideo(videoPath, m_lastAnalysis);
        L.logNote("analyzeImportedVideo — step G: status update");
        statusBar()->showMessage(
            QString("Ready — %1 frames  I:%2  P:%3  B:%4")
                .arg(m_lastAnalysis.totalFrames)
                .arg(m_lastAnalysis.iFrameCount)
                .arg(m_lastAnalysis.pFrameCount)
                .arg(m_lastAnalysis.bFrameCount));
    } else {
        statusBar()->showMessage("Analysis failed: " + m_lastAnalysis.errorMessage);
        QMessageBox::warning(this, "Analysis Failed", m_lastAnalysis.errorMessage);
    }
    L.logNote("analyzeImportedVideo — DONE");
}

void MainWindow::populateTimeline(const AnalysisReport& report)
{
    QVector<char> types;
    QVector<bool> idrs;
    types.reserve(report.frames.size());
    idrs.reserve(report.frames.size());

    for (const FrameInfo& f : report.frames) {
        types.append(f.pictType == '\0' ? '?' : f.pictType);
        idrs.append(f.keyFrame);
    }

    m_timeline->loadVideo(m_currentVideoPath, types, idrs);
    m_timeline->clearSelection();
    setTransformButtonsEnabled(false);
    m_quickMosh->setMoshEnabled(!m_transformBusy);
    m_selectionLabel->setText(
        QString("0 / %1 frames selected").arg(report.frames.size()));
}

// =============================================================================
// Selection handling
// =============================================================================

void MainWindow::onSelectionChanged(const QVector<int>& selected)
{
    int n = selected.size();
    if (n == 0) {
        m_selectionLabel->setText(
            QString("0 / %1 frames selected").arg(m_lastAnalysis.frames.size()));
        setTransformButtonsEnabled(false);
    } else {
        m_selectionLabel->setText(
            QString("%1 frame%2 selected").arg(n).arg(n == 1 ? "" : "s"));
        setTransformButtonsEnabled(!m_transformBusy);

        int earliest = selected.first();
        for (int idx : selected) if (idx < earliest) earliest = idx;
        m_mbWidget->navigateToFrame(earliest);
    }
}

void MainWindow::setTransformButtonsEnabled(bool enabled)
{
    m_btnForceI    ->setEnabled(enabled);
    m_btnForceP    ->setEnabled(enabled);
    m_btnForceB    ->setEnabled(enabled);
    m_btnDelete    ->setEnabled(enabled);
    m_btnDupLeft   ->setEnabled(enabled);
    m_btnDupRight  ->setEnabled(enabled);
    m_btnInterpLeft ->setEnabled(enabled);
    m_btnInterpRight->setEnabled(enabled);
    m_btnFlip       ->setEnabled(enabled);
    m_btnFlop       ->setEnabled(enabled);
    m_quickMosh->setMoshEnabled(!m_transformBusy && !m_currentVideoPath.isEmpty());
}

// =============================================================================
// Frame type conversion
// =============================================================================

void MainWindow::onForceI()        { startTransform(FrameTransformerWorker::ForceI); }
void MainWindow::onForceP()        { startTransform(FrameTransformerWorker::ForceP); }
void MainWindow::onForceB()        { startTransform(FrameTransformerWorker::ForceB); }
void MainWindow::onDeleteFrames()  { startTransform(FrameTransformerWorker::DeleteFrames); }
void MainWindow::onDupLeft()       { startTransform(FrameTransformerWorker::DuplicateLeft); }
void MainWindow::onDupRight()      { startTransform(FrameTransformerWorker::DuplicateRight); }
void MainWindow::onFlip()          { startTransform(FrameTransformerWorker::FlipVertical); }
void MainWindow::onFlop()          { startTransform(FrameTransformerWorker::FlipHorizontal); }
static int askInterpolateCount(QWidget* parent, const QString& direction)
{
    bool ok = false;
    int n = QInputDialog::getInt(
        parent,
        QString("Interpolate %1").arg(direction),
        "Number of intermediate frames to insert:",
        5,          // default
        1,          // min
        120,        // max
        1,          // step
        &ok);
    return ok ? n : 0;  // 0 = user cancelled
}

void MainWindow::onInterpLeft()
{
    int n = askInterpolateCount(this, "Left");
    if (n > 0) startTransform(FrameTransformerWorker::InterpolateLeft,
                               GlobalEncodeParams(), n);
}

void MainWindow::onInterpRight()
{
    int n = askInterpolateCount(this, "Right");
    if (n > 0) startTransform(FrameTransformerWorker::InterpolateRight,
                               GlobalEncodeParams(), n);
}
void MainWindow::onApplyGlobalParams()
{
    startTransform(FrameTransformerWorker::MBEditOnly, m_globalParams->currentParams());
}

// =============================================================================
// Quick Mosh — PLACEHOLDER
// =============================================================================
// Built-in Quick Mosh presets removed. This section will be re-populated
// after MB Editor and Global Encode presets are validated.

#if 0  // ── onQuickMosh removed — no built-in Quick Mosh presets ──────────────
static void onQuickMosh_REMOVED() {
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;

    const int total = m_lastAnalysis.frames.size();
    if (total == 0) return;

    // Find first P-frame to use as the corruption seed.
    int seedFrame = 1;
    for (int i = 1; i < total; ++i) {
        if (m_lastAnalysis.frames[i].pictType == 'P') { seedFrame = i; break; }
    }
    const int fullCascade = qMax(0, total - seedFrame - 1);

    // ── Helpers ────────────────────────────────────────────────────────────────

    // Build a whole-frame FrameMBParams (selectedMBs empty = all MBs).
    auto makeSeed = [&](int refDepth, int ghostBlend,
                        int mvDriftX, int mvDriftY, int mvAmplify,
                        int noiseLevel, int pixelOffset, int invertLuma,
                        int chromaDriftX, int chromaDriftY, int chromaOffset,
                        int qpDelta, int spillRadius, int sampleRadius,
                        int refScatter, int blockFlatten,
                        int colorTwistU, int colorTwistV,
                        int cascadeLen, int cascadeDecay) -> FrameMBParams {
        FrameMBParams p;
        p.refDepth     = refDepth;
        p.ghostBlend   = ghostBlend;
        p.mvDriftX     = mvDriftX;
        p.mvDriftY     = mvDriftY;
        p.mvAmplify    = mvAmplify;
        p.noiseLevel   = noiseLevel;
        p.pixelOffset  = pixelOffset;
        p.invertLuma   = invertLuma;
        p.chromaDriftX = chromaDriftX;
        p.chromaDriftY = chromaDriftY;
        p.chromaOffset = chromaOffset;
        p.qpDelta      = qpDelta;
        p.spillRadius  = spillRadius;
        p.sampleRadius = sampleRadius;
        p.refScatter   = refScatter;
        p.blockFlatten = blockFlatten;
        p.colorTwistU  = colorTwistU;
        p.colorTwistV  = colorTwistV;
        p.cascadeLen   = cascadeLen;
        p.cascadeDecay = cascadeDecay;
        return p;
    };

    // Stamp the same params on every frame (for per-frame effects).
    auto stampAll = [&](MBEditMap& em, const FrameMBParams& tmpl) {
        for (int i = 0; i < total; ++i) em[i] = tmpl;
    };

    // Plant burst seeds at regular intervals starting from seedFrame.
    // Each burst cascades for cascadeLen frames then decays; the gap before
    // the next seed is the "ebb" — video recovers toward normal, then breaks again.
    auto plantSeeds = [&](MBEditMap& em, int interval, const FrameMBParams& seed) {
        for (int f = seedFrame; f < total; f += interval)
            em[f] = seed;
    };

    // ── Preset definitions ─────────────────────────────────────────────────────
    // Each preset owns its GlobalEncodeParams from scratch — the panel is ignored.

    MBEditMap edits;
    GlobalEncodeParams gp;   // default-constructed (-1 = keep encoder default)

    switch (presetIndex) {

    case 0: { // ── P-Frame River ─────────────────────────────────────────────
        // Pure datamosh baseline: infinite P-frame chain, no MB manipulation.
        // Lets the encoder's temporal prediction do the work on its own.
        gp.gopSize      = 0;
        gp.killIFrames  = true;
        gp.bFrames      = 0;
        gp.bAdapt       = 0;
        gp.refFrames    = 4;
        gp.subpelRef    = 2;
        gp.meMethod     = 1;   // hex — slight inaccuracy for organic smear
        gp.trellis      = 0;
        gp.mbTreeDisable = true;
        // No MB edits — pure encoder-structure effect
        break;
    }

    case 1: { // ── Déjà Vu ───────────────────────────────────────────────────
        // Deep temporal echo: full-cascade ghost blend from 3 frames back
        // with a diffuse sample radius to blur the echo source.
        gp.gopSize      = 0;
        gp.killIFrames  = true;
        gp.bFrames      = 0;
        gp.refFrames    = 8;
        gp.subpelRef    = 1;
        gp.trellis      = 0;
        gp.mbTreeDisable = true;
        edits[seedFrame] = makeSeed(3, 60, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 2, 0, 0, 0, 0, fullCascade, 45);
        break;
    }

    case 2: { // ── Block Cathedral ───────────────────────────────────────────
        // 16×16-only partitions, deblocking off, block-flatten every frame.
        // The encoder sees perfectly flat reference blocks → massive boundary
        // artifacts and a stained-glass mosaic aesthetic.
        gp.gopSize      = 0;
        gp.killIFrames  = true;
        gp.partitionMode = 0;  // 16×16 only
        gp.subpelRef    = 0;
        gp.trellis      = 0;
        gp.noDeblock    = true;
        gp.use8x8DCT    = false;
        gp.meMethod     = 0;   // diamond — least accurate
        gp.meRange      = 8;
        gp.mbTreeDisable = true;
        stampAll(edits, makeSeed(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                 0, 0, 0, 0, 40, 0, 0, 0, 0));
        break;
    }

    case 3: { // ── Warp Smear ─────────────────────────────────────────────────
        // Strong diagonal MV drift at 3× amplification, seeded once and
        // cascaded with slow decay for a fluid liquid-warp throughout.
        gp.gopSize      = 0;
        gp.killIFrames  = true;
        gp.refFrames    = 8;
        gp.noDeblock    = true;
        gp.subpelRef    = 0;
        gp.meMethod     = 0;
        gp.trellis      = 0;
        gp.mbTreeDisable = true;
        edits[seedFrame] = makeSeed(1, 35, 50, 20, 3, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, fullCascade, 20);
        break;
    }

    case 4: { // ── VHS Ghost ──────────────────────────────────────────────────
        // Chroma drift independent of luma + ghost blend cascade.
        // Produces a warm tape-worn colour-separated echo trail.
        gp.gopSize      = 0;
        gp.killIFrames  = true;
        gp.refFrames    = 6;
        gp.trellis      = 0;
        gp.mbTreeDisable = true;
        edits[seedFrame] = makeSeed(1, 30, 0, 0, 1, 0, 0, 0, 30, 0, 15,
                                    0, 0, 0, 0, 0, 0, 0, fullCascade, 40);
        break;
    }

    case 5: { // ── Corrupted Signal ───────────────────────────────────────────
        // Heavy noise + reference scatter + forced high-QP range on every frame.
        // No temporal cascade — raw per-frame digital disintegration.
        gp.gopSize       = 0;
        gp.qpMin         = 28;
        gp.qpMax         = 51;
        gp.noDeblock     = true;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        gp.noFastPSkip   = true;
        gp.noDctDecimate = true;
        gp.meMethod      = 0;
        stampAll(edits, makeSeed(1, 0, 0, 0, 1, 100, 0, 0, 0, 0, 0,
                                 20, 0, 0, 8, 0, 0, 0, 0, 0));
        break;
    }

    case 6: { // ── Temporal Collapse ──────────────────────────────────────────
        // 8 B-frames with 16-frame reference depth + temporal direct mode.
        // Ghost-blend cascade stacks complex multi-directional temporal predictions.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 8;
        gp.bAdapt        = 2;
        gp.refFrames     = 16;
        gp.directMode    = 1;   // temporal
        gp.weightedPredB = true;
        gp.weightedPredP = 2;
        gp.mbTreeDisable = true;
        edits[seedFrame] = makeSeed(4, 50, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 3, 0, 0, 0, 0, fullCascade, 30);
        break;
    }

    case 7: { // ── Welcome to LA - 10 ────────────────────────────────────────
        // Subtle ebb-and-flow pulse: bursts every ~45 frames, short cascade,
        // slow decay. Subjects remain mostly recognisable throughout.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 4;
        gp.noDeblock     = true;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.trellis       = 0;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 45,
                   makeSeed(1, 35, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 22, 55));
        break;
    }

    case 8: { // ── Welcome to LA - 20 ────────────────────────────────────────
        // Moderate pulse: bursts every ~35 frames, deeper ghost blend,
        // cascade lasts longer. Subjects readable but visibly dissolving at hits.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 6;
        gp.noDeblock     = true;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.trellis       = 0;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 35,
                   makeSeed(2, 50, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 25, 45));
        break;
    }

    case 9: { // ── Welcome to LA - 30 ────────────────────────────────────────
        // Strong pulse: bursts every ~25 frames + MV drift. The encoder
        // repeats macroblocks across neighbours; subjects abstract during bursts.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.trellis       = 0;
        gp.noFastPSkip   = true;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 25,
                   makeSeed(2, 62, 15, 0, 2, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 28, 38));
        break;
    }

    case 10: { // ── Welcome to LA - 40 ───────────────────────────────────────
        // Intense pulse: bursts every ~18 frames, deeper reference, 3× MV
        // amplification. Video barely recovers; block echoes spread widely.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 12;
        gp.noDeblock     = true;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.trellis       = 0;
        gp.noFastPSkip   = true;
        gp.noDctDecimate = true;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 18,
                   makeSeed(3, 75, 25, 0, 3, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 32, 28));
        break;
    }

    case 11: { // ── Welcome to LA - NOT TO 50 YOU PSYCHO!!!! ─────────────────
        // Maximum destruction: bursts every 10 frames. ghostBlend=88, refDepth=4,
        // 4× MV amplification, QP corruption, noise, and 16×16 block forcing.
        // The video becomes a cascading macroblock avalanche. You were warned.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.trellis       = 0;
        gp.noFastPSkip   = true;
        gp.noDctDecimate = true;
        gp.partitionMode = 0;  // 16×16 only — maximise block boundary artifacts
        gp.use8x8DCT     = false;
        gp.qpMax         = 51;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 10,
                   makeSeed(4, 88, 40, 15, 4, 40, 0, 0, 0, 0, 0,
                            25, 2, 0, 0, 0, 0, 0, 38, 15));
        break;
    }

    case 12: { // ── Block Wars ─────────────────────────────────────────────────
        // Strong horizontal MV displacement from 2 frames back, seeded every ~30 frames.
        // cascadeDecay=0 = hard freeze: cascade frames get ghostBlend=100 + qpDelta=51
        // (skip MBs, zero residual). Blocks land at wrong positions and stay there until
        // new motion punches through. No internal scatter — clean block edges (Minecraft).
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.partitionMode = 0;   // 16×16 only — maximise visible block size
        gp.subpelRef     = 0;   // integer-pixel MVs → step artifacts
        gp.meMethod      = 0;   // diamond — least accurate → wrong MVs
        gp.meRange       = 4;   // extremely narrow search → encoder assigns wrong MVs
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        // Seed: pure MV displacement — no ghost blend on seed itself so there is no
        // haze; just wrong pixels baked in. Cascade freezes them hard (cascadeDecay=0).
        plantSeeds(edits, 30,
                   makeSeed(2, 0, 60, 0, 4, 0, 0, 0, 0, 0, 0,
                            40, 0, 0, 0, 0, 0, 0, 22, 0));
        break;
    }

    case 13: { // ── Blockaderade ───────────────────────────────────────────────
        // Diagonal MV displacement from 3 frames back + heavy internal scatter.
        // refScatter=20 randomises where each pixel inside the 16×16 block samples
        // from in the reference, so blocks appear as shattered fragments of a distant
        // frame rather than clean copies. Hard freeze, zero decay. Seeds every ~25 frames.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 4;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 25,
                   makeSeed(3, 0, 45, 30, 3, 0, 0, 0, 0, 0, 0,
                            35, 0, 0, 20, 0, 0, 0, 18, 0));
        break;
    }

    case 14: { // ── COVID BLOCK DOWN ───────────────────────────────────────────
        // Maximum block chaos: extreme displacement (5× amplify) from 4 frames back,
        // heavy internal scatter, qpDelta=51 on the seed itself forces maximum
        // quantisation corruption at the moment of impact. meRange=4 means the encoder
        // cannot find good predictions anywhere — it makes wrong inter-predictions on
        // top of the already-wrong pixel content. Seeds every ~15 frames so recovery
        // barely happens before the next hit. You cannot leave the house.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 4;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.noDctDecimate = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 15,
                   makeSeed(4, 0, 80, -50, 5, 0, 0, 0, 0, 0, 0,
                            51, 0, 0, 28, 0, 0, 0, 12, 0));
        break;
    }

    case 15: { // ── Stepping on LEGGO ──────────────────────────────────────────
        // Every single frame is stamped with a horizontal MV displacement from
        // 2 frames back at 3× amplification.  No cascade, no seeds — the block smear
        // is continuous across the entire clip.  qpDelta=20 encourages the encoder to
        // lean on the (wrong) prediction and emit large inter blocks rather than fixing
        // the error with fine residuals.  Blocks slide rightward like tiles on ice.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 8;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(2, 0, 25, 0, 3, 0, 0, 0, 0, 0, 0,
                                 20, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 16: { // ── Ablockalypse Now ───────────────────────────────────────────
        // Single large displacement seed every ~45 frames, cascadeLen=40, cascadeDecay=8.
        // The trail runs for 40 frames before barely easing off — nearly the full gap
        // between seeds is spent in a block-smear state with only a brief window of
        // clarity before the next seed fires.  Deep reference (3 frames back) and
        // strong horizontal displacement create a sustained lego-block river.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 4;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 45,
                   makeSeed(3, 0, 80, 0, 3, 0, 0, 0, 0, 0, 0,
                            40, 0, 0, 0, 0, 0, 0, 40, 8));
        break;
    }

    case 17: { // ── Road to Prediction ─────────────────────────────────────────
        // Seeds every 5 frames each trigger a 4-frame hard-freeze cascade (decay=0).
        // Frame N: blocks displaced.  Frames N+1..N+4: ghostBlend=100 + qpDelta=51
        // locks them in place.  Frame N+5: new displacement.  Blocks advance in
        // discrete jolts then hold their wrong position before the next jolt.
        // The rhythmic step-freeze-step pattern is unlike any continuous drift preset.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 6;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 5,
                   makeSeed(2, 0, 35, 0, 3, 0, 0, 0, 0, 0, 0,
                            30, 0, 0, 0, 0, 0, 0, 4, 0));
        break;
    }

    case 18: { // ── Why can't I do anything right? ────────────────────────────
        // Diagonal block trails stamped on every frame at 5× amplification.
        // The effective displacement is 100px X and 75px Y — blocks are sourced from
        // very far off-axis positions in the reference frame.  High qpDelta forces
        // coarse quantisation so the encoder cannot correct the extreme offset;
        // it must use the wrong prediction as-is.  Everything trails diagonally.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 12;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 6;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(2, 0, 20, 15, 5, 0, 0, 0, 0, 0, 0,
                                 35, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 19: { // ── SHATTERED EGO ──────────────────────────────────────────────
        // spillRadius=5 makes each seed infect an 11×11 MB neighbourhood as one
        // coordinated zone.  The whole zone displaces diagonally then hard-freezes
        // for 17 frames (decay=0).  Seeds every ~20 frames.  Large groups of blocks
        // move as a single unit rather than individual 16×16 tiles — the smear area
        // is much wider than any other preset in this collection.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 4;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 20,
                   makeSeed(2, 0, 55, 35, 3, 0, 0, 0, 0, 0, 0,
                            35, 5, 0, 0, 0, 0, 0, 17, 0));
        break;
    }

    case 20: { // ── LEGGO MY EGO ───────────────────────────────────────────────
        // Every frame stamped from content 5 frames back at 4× diagonal amplification.
        // refDepth=5 means the reference has already been corrupted 4 additional times
        // by prior encodes of those frames — the displacement compounds through the
        // temporal chain into something the encoder cannot unravel.  High qpDelta on
        // every frame prevents any correction.  Maximum sustained compound block trail.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 16;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 4;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.noDctDecimate = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(5, 0, 30, 20, 4, 0, 0, 0, 0, 0, 0,
                                 35, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 21: { // ── What is real? ───────────────────────────────────────────────
        // Seeds every 4 frames, cascadeLen=3, cascadeDecay=0.
        // meRange=16 (wider than block-war presets) lets the encoder find good
        // predictions in static areas → those MBs skip cleanly.  Only motion areas,
        // where the encoder cannot reconcile our pixel manipulation with a good match,
        // accumulate wrong inter-predictions → small scattered block artifacts there.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 4;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 1;
        gp.meMethod      = 1;   // hex — better accuracy in static areas
        gp.meRange       = 16;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 4,
                   makeSeed(1, 0, 18, 0, 2, 0, 0, 0, 0, 0, 0,
                            15, 0, 0, 0, 0, 0, 0, 3, 0));
        break;
    }

    case 22: { // ── Mandella Effect ────────────────────────────────────────────
        // 3-frame seed interval, cascadeLen=2, cascadeDecay=0.
        // Extremely rapid cycle: 1 displaced frame, 2 frozen, repeat.
        // Very short windows mean effects never fully commit — blocks flicker
        // and scatter constantly at motion edges, each wrong frame becoming the
        // reference for the next.  meRange=20 maximises static-area skipping.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 4;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 1;
        gp.meMethod      = 1;
        gp.meRange       = 20;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 3,
                   makeSeed(2, 0, 12, 8, 2, 0, 0, 0, 0, 0, 0,
                            20, 0, 0, 0, 0, 0, 0, 2, 0));
        break;
    }

    case 23: { // ── 12 years Sober ─────────────────────────────────────────────
        // Seeds every 10 frames, cascadeLen=8, cascadeDecay=0.
        // Pure upward drift (mvDriftY=-22) at 3× amplification: blocks are sourced
        // from 2 frames back, shifted 66px upward.  Vertical subject motion creates
        // upward smear trails; horizontal-only areas skip cleanly.  Cascade runs
        // for 8 hard frames giving the trail real duration before next seed.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 6;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 12;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 10,
                   makeSeed(2, 0, 0, -22, 3, 0, 0, 0, 0, 0, 0,
                            22, 0, 0, 0, 0, 0, 0, 8, 0));
        break;
    }

    case 24: { // ── Halifax Explosion ──────────────────────────────────────────
        // Seeds every 8 frames at 4× amplification (effective 220px horizontal,
        // 140px upward displacement); cascadeLen=5, cascadeDecay=0.
        // Very high amplitude but short cascade: each seed is a violent block
        // detonation that impacts and clears in 5 frames, leaving almost normal
        // video before the next explosion.  meRange=8 keeps encoder accuracy low.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 8;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 8,
                   makeSeed(2, 0, 55, -35, 4, 0, 0, 0, 0, 0, 0,
                            40, 0, 0, 0, 0, 0, 0, 5, 0));
        break;
    }

    case 25: { // ── Rainbow Road ───────────────────────────────────────────────
        // Every frame stamped with horizontal block drift PLUS colorTwistU=+80 /
        // colorTwistV=-80 on all motion-area blocks → strong magenta-red hue shift
        // wherever the encoder assigns inter blocks instead of skip.  Static areas
        // skip cleanly and keep correct colour; motion areas accumulate coloured
        // block trails.  No cascade — the hue corruption is fully continuous.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 6;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 1;
        gp.meMethod      = 1;
        gp.meRange       = 16;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(2, 0, 20, 0, 2, 0, 0, 0, 0, 0, 0,
                                 20, 0, 0, 0, 0, 80, -80, 0, 0));
        break;
    }

    case 26: { // ── Ego Death ──────────────────────────────────────────────────
        // Seeds every 6 frames, cascadeLen=5, cascadeDecay=0.
        // colorTwistU=-100 / colorTwistV=+100 on seed frames = strong cyan-green.
        // chromaDriftX=50 carries colour-plane fringing through every cascade frame
        // (cascade propagates chromaDrift scaled by the fade factor).
        // Diagonal block drift at 3×; motion areas smear in cyan, then freeze.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 12;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 6,
                   makeSeed(2, 0, 25, -15, 3, 0, 0, 0, 50, 0, 0,
                            30, 0, 0, 0, 0, -100, 100, 5, 0));
        break;
    }

    case 27: { // ── K Hole ─────────────────────────────────────────────────────
        // Every frame stamped from 3 frames back; colorTwistU=+70 / colorTwistV=+70
        // shifts both chroma axes in the same direction → deep magenta-purple zone.
        // chromaDriftY=+30 vertically offsets colour planes from luma, creating
        // vertical colour halos at block edges.  chromaOffset=+20 lifts saturation.
        // Motion blocks bloom into purple clusters; static areas skip clean.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 8;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(3, 0, 15, 15, 2, 0, 0, 0, 0, 30, 20,
                                 25, 0, 0, 0, 0, 70, 70, 0, 0));
        break;
    }

    case 28: { // ── Hyaluronic Acid ────────────────────────────────────────────
        // Seeds every 5 frames, cascadeLen=4, cascadeDecay=0.
        // chromaDriftX=60 slides colour planes 60px horizontally relative to luma
        // → vivid colour halos at every hard block edge; this fringing carries
        // through the cascade (chromaDrift is propagated).
        // colorTwistU=-50 / colorTwistV=+80 warms the seed hue.
        // Diagonal luma drift at 2× keeps block displacement subtle.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 6;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 1;
        gp.meMethod      = 1;
        gp.meRange       = 16;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 5,
                   makeSeed(2, 0, 18, -12, 2, 0, 0, 0, 60, -30, 0,
                            20, 0, 0, 0, 0, -50, 80, 4, 0));
        break;
    }

    case 29: { // ── You belong in Prison ───────────────────────────────────────
        // Every frame stamped with colorTwistU=+127 / colorTwistV=-127
        // (maximum possible chrominance distortion in both axes simultaneously)
        // plus chromaOffset=-60 (heavy chroma suppression underneath the twist).
        // Horizontal block drift at 3× runs the full clip continuously.
        // There is no parole.  There is no early release.  You did this.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 12;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 0;
        gp.meMethod      = 0;
        gp.meRange       = 8;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.qpMax         = 51;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        stampAll(edits, makeSeed(2, 0, 22, 0, 3, 0, 0, 0, 0, 0, -60,
                                 35, 0, 0, 0, 0, 127, -127, 0, 0));
        break;
    }

    case 30: { // ── Ohgee ──────────────────────────────────────────────────────
        // The balanced OG: seeds every 5 frames, cascadeLen=6, cascadeDecay=0.
        // Diagonal 3× luma drift from 2 frames back; chromaDriftX=45 carries
        // colour-plane fringing through every cascade frame; colorTwistU=+60 /
        // colorTwistV=-70 on seed frames.  meRange=16 keeps artifacts naturally
        // scattered at motion boundaries.  Everything working together.
        gp.gopSize       = 0;
        gp.killIFrames   = true;
        gp.bFrames       = 0;
        gp.refFrames     = 8;
        gp.noDeblock     = true;
        gp.partitionMode = 0;
        gp.subpelRef     = 1;
        gp.meMethod      = 1;
        gp.meRange       = 16;
        gp.trellis       = 0;
        gp.use8x8DCT     = false;
        gp.mbTreeDisable = true;
        gp.psyRD         = 0.0f;
        gp.aqMode        = 0;
        plantSeeds(edits, 5,
                   makeSeed(2, 0, 28, 18, 3, 0, 0, 0, 45, 0, 0,
                            25, 0, 0, 0, 0, 60, -70, 6, 0));
        break;
    }

    case 31: { // ── The Deep ──────────────────────────────────────────────────
        // Every frame displaced from 7 frames back (max refDepth). Wide meRange=24
        // means the encoder finds good predictions in static areas (skip MBs) and
        // accumulates wrong predictions only at motion — a deep temporal block ocean.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        stampAll(edits, makeSeed(7, 0, 8, 5, 2, 0, 0, 0, 0, 0, 0,
                                 12, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 32: { // ── Undertow ──────────────────────────────────────────────────
        // Horizontal drift oscillates across 3 full sine cycles using a per-seed
        // loop. Seeds every 5 frames with a 4-frame hard freeze. Blocks surge
        // left, ease, surge right in a rhythmic tidal current pattern.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(55.0f * sinf(t * 6.0f * 3.14159265f));
                edits[f] = makeSeed(3, 0, dx, 0, 2, 0, 0, 0, 0, 0, 0,
                                    15, 0, 0, 0, 0, 0, 0, 4, 0);
            }
        }
        break;
    }

    case 33: { // ── Riptide ───────────────────────────────────────────────────
        // Seeds every 3 frames alternate direction: +45 then -45. 2-frame hard
        // freeze at 3x amplification. Chaotic crosscurrent of battling block forces.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 18; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 3;
            int idx = 0;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                int dx = (idx % 2 == 0) ? 45 : -45;
                edits[f] = makeSeed(2, 0, dx, 0, 3, 0, 0, 0, 0, 0, 0,
                                    20, 0, 0, 0, 0, 0, 0, 2, 0);
            }
        }
        break;
    }

    case 34: { // ── Kelp Forest ───────────────────────────────────────────────
        // Vertical drift oscillates across 4 sine cycles. Seeds every 7 frames
        // with a 5-frame hard freeze. Blocks sway up then down like underwater kelp.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 7;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dy = (int)(48.0f * sinf(t * 8.0f * 3.14159265f));
                edits[f] = makeSeed(3, 0, 0, dy, 2, 0, 0, 0, 0, 0, 0,
                                    15, 0, 0, 0, 0, 0, 0, 5, 0);
            }
        }
        break;
    }

    case 35: { // ── Tsunami ───────────────────────────────────────────────────
        // Rare massive horizontal seeds every 80 frames, each spawning a 60-frame
        // cascade at 5x amplification. One event is a wall of blocks that colonises
        // the frame for a very long time before slowly subsiding.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 0;
        gp.meRange = 16; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 80, makeSeed(4, 0, 100, 0, 5, 0, 0, 0, 0, 0, 0,
                                       30, 0, 0, 0, 0, 0, 0, 60, 3));
        break;
    }

    case 36: { // ── Whirlpool ─────────────────────────────────────────────────
        // Circular drift via sin/cos traces 4 full rotations at 60px amplitude.
        // Seeds every 5 frames with a 4-frame hard freeze. Blocks spiral in
        // coordinated clockwise loops across the entire clip.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(60.0f * sinf(t * 8.0f * 3.14159265f));
                int dy = (int)(60.0f * cosf(t * 8.0f * 3.14159265f));
                edits[f] = makeSeed(3, 0, dx, dy, 2, 0, 0, 0, 0, 0, 0,
                                    20, 0, 0, 0, 0, 0, 0, 4, 0);
            }
        }
        break;
    }

    case 37: { // ── Bioluminescence ───────────────────────────────────────────
        // Every frame stamped with slow diagonal drift from 5 frames back.
        // chromaDriftX=25 offsets colour planes horizontally; colorTwistU=-70 /
        // colorTwistV=+60 makes motion blocks bloom in a cyan-green hue.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 16; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        stampAll(edits, makeSeed(5, 0, 18, 12, 2, 0, 0, 0, 25, 0, 0,
                                 0, 0, 0, 0, 0, -70, 60, 0, 0));
        break;
    }

    case 38: { // ── The Abyss ─────────────────────────────────────────────────
        // Maximum refDepth=7 with very minimal drift. meRange=24 means the encoder
        // finds clean predictions in static areas and concentrates block errors only
        // at motion zones. A barely-moving wall of temporally ancient blocks.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        stampAll(edits, makeSeed(7, 0, 5, 3, 2, 0, 0, 0, 0, 0, 0,
                                 15, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 39: { // ── Coral Reef ────────────────────────────────────────────────
        // Circular drift traces 2 full rotations at 30px amplitude. Seeds every
        // 3 frames with a 2-frame freeze. meRange=18 scatters effects naturally
        // at motion. A diverse, scattered colony of small circular block motion.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 18; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 3;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(30.0f * sinf(t * 4.0f * 3.14159265f));
                int dy = (int)(30.0f * cosf(t * 4.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, dx, dy, 2, 0, 0, 0, 0, 0, 0,
                                    10, 0, 0, 0, 0, 0, 0, 2, 0);
            }
        }
        break;
    }

    case 40: { // ── Monsoon ───────────────────────────────────────────────────
        // Every frame displaced downward at 3x from 2 frames back. mvDriftY=30
        // with mvDriftX=5 for slight diagonal. Blocks cascade downward like
        // relentless tropical rain throughout the entire clip.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 12; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        stampAll(edits, makeSeed(2, 0, 5, 30, 3, 0, 0, 0, 0, 0, 0,
                                 10, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 41: { // ── The Gyre ──────────────────────────────────────────────────
        // Growing spiral: radius expands from 12px to 70px across 6 full rotations.
        // Seeds every 5 frames, 4-frame hard freeze. Blocks begin tight loops then
        // fan out into wide spiralling arcs as the clip progresses.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                float r = 12.0f + 58.0f * t;
                int dx = (int)(r * sinf(t * 12.0f * 3.14159265f));
                int dy = (int)(r * cosf(t * 12.0f * 3.14159265f));
                dx = dx < -128 ? -128 : (dx > 128 ? 128 : dx);
                dy = dy < -128 ? -128 : (dy > 128 ? 128 : dy);
                edits[f] = makeSeed(3, 0, dx, dy, 2, 0, 0, 0, 0, 0, 0,
                                    20, 0, 0, 0, 0, 0, 0, 4, 0);
            }
        }
        break;
    }

    case 42: { // ── Sea Foam ──────────────────────────────────────────────────
        // Very frequent seeds every 2 frames, each spawning a 1-frame freeze at
        // 2x amplification. meRange=24 concentrates small block groups at motion
        // only. A perpetual light froth of displaced pixels across the whole clip.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 2, makeSeed(2, 0, 12, 0, 2, 0, 0, 0, 0, 0, 0,
                                      10, 0, 0, 0, 0, 0, 0, 1, 0));
        break;
    }

    case 43: { // ── Rogue Wave ────────────────────────────────────────────────
        // Enormous horizontal seeds every 90 frames spawn 60-frame cascades at
        // 5x amplification. Each event is a singular wall of blocks that colonises
        // the frame for a prolonged burst before the next detonation.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 0;
        gp.meRange = 16; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 90, makeSeed(4, 0, 110, 0, 5, 0, 0, 0, 0, 0, 0,
                                       35, 0, 0, 0, 0, 0, 0, 60, 2));
        break;
    }

    case 44: { // ── Gulf Stream ───────────────────────────────────────────────
        // Every frame displaced horizontally from 3 frames back at 2x amplification.
        // colorTwistU=+30 warms motion blocks with an orange-amber cast. A continuous
        // warm westward ocean current running through the entire clip.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 16; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        stampAll(edits, makeSeed(3, 0, 25, 0, 2, 0, 0, 0, 0, 0, 0,
                                 10, 0, 0, 0, 0, 30, 0, 0, 0));
        break;
    }

    case 45: { // ── Vortex Current ────────────────────────────────────────────
        // 8 full circular rotations via sin/cos at 45px amplitude. Seeds every
        // 4 frames with a 3-frame hard freeze. Blocks spin in rapid tight circles
        // throughout the whole clip — fast, hypnotic, relentless.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 4;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(45.0f * sinf(t * 16.0f * 3.14159265f));
                int dy = (int)(45.0f * cosf(t * 16.0f * 3.14159265f));
                edits[f] = makeSeed(3, 0, dx, dy, 2, 0, 0, 0, 0, 0, 0,
                                    15, 0, 0, 0, 0, 0, 0, 3, 0);
            }
        }
        break;
    }

    case 46: { // ── Dead Calm ─────────────────────────────────────────────────
        // Rare seeds every 60 frames each spawn a 55-frame cascade with slow decay.
        // meRange=20 concentrates errors at motion zones. The ocean barely moves,
        // then shifts slowly and for a very long time before the next event.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 60, makeSeed(4, 0, 15, 0, 2, 0, 0, 0, 0, 0, 0,
                                       10, 0, 0, 0, 0, 0, 0, 55, 2));
        break;
    }

    case 47: { // ── Sonar Ping ────────────────────────────────────────────────
        // Seeds every 6 frames with short 2-frame hard-freeze cascades at 3x
        // diagonal displacement. Narrow meRange=10 creates dense block clusters
        // at each ping before fully clearing for the next outward pulse.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 0;
        gp.meRange = 10; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 6, makeSeed(2, 0, 40, -25, 3, 0, 0, 0, 0, 0, 0,
                                      20, 0, 0, 0, 0, 0, 0, 2, 0));
        break;
    }

    case 48: { // ── Coriolis ──────────────────────────────────────────────────
        // Drift traces exactly one full circle across the entire clip. Seeds every
        // 7 frames with a 5-frame hard freeze at 50px amplitude. Blocks circle once
        // in a slow geophysical arc — the whole clip is one planetary rotation.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 7;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(50.0f * sinf(t * 2.0f * 3.14159265f));
                int dy = (int)(50.0f * cosf(t * 2.0f * 3.14159265f));
                edits[f] = makeSeed(3, 0, dx, dy, 2, 0, 0, 0, 0, 0, 0,
                                    15, 0, 0, 0, 0, 0, 0, 5, 0);
            }
        }
        break;
    }

    case 49: { // ── The Hadal Zone ────────────────────────────────────────────
        // Every frame stamped with strong downward displacement from 7 frames back
        // at 3x amplification. qpDelta=35 forces coarse block quantisation.
        // meRange=8 forces wrong predictions everywhere — maximum crushing depth.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 0;
        gp.meRange = 8; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        gp.qpMax = 51;
        stampAll(edits, makeSeed(7, 0, 0, 35, 3, 0, 0, 0, 0, 0, 0,
                                 35, 0, 0, 0, 0, 0, 0, 0, 0));
        break;
    }

    case 50: { // ── Poseidon ──────────────────────────────────────────────────
        // 5 full circular rotations at 70px amplitude from 4 frames back. Each seed
        // carries chromaDriftX=40 and colorTwistU=+80 / colorTwistV=-90 (deep
        // magenta-red hue). 3-frame hard freeze. Peak ocean-god block chaos.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        gp.qpMax = 51;
        {
            const int interval = 4;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(70.0f * sinf(t * 10.0f * 3.14159265f));
                int dy = (int)(70.0f * cosf(t * 10.0f * 3.14159265f));
                dx = dx < -128 ? -128 : (dx > 128 ? 128 : dx);
                dy = dy < -128 ? -128 : (dy > 128 ? 128 : dy);
                edits[f] = makeSeed(4, 0, dx, dy, 3, 0, 0, 0, 40, 0, 0,
                                    25, 0, 0, 0, 0, 80, -90, 3, 0);
            }
        }
        break;
    }

    // =========================================================================
    // =========================================================================
    // Cases 71-75: Block Trail Echoes — mosaic block smear at motion only.
    //
    // MECHANISM (revised):
    //   • Seed: ghostBlend=0 (no global pixel blending), blockFlatten=80-100
    //     collapses every MB's luma to its spatial average — a flat uniform
    //     colour. qpDelta=51 on the seed forces the encoder to zero residual,
    //     so the flat blocks survive into the output unchanged.
    //   • This creates one frame of full-frame mosaic at seed time.
    //   • Cascade (ghostBlend=100, qpDelta=51, cascadeDecay=0): copies the
    //     previous encoded frame verbatim. For static MBs the flat block at
    //     the correct colour is copied → looks like subtle pixelation.
    //     For motion MBs the flat block at the WRONG position is copied →
    //     hard frozen rectangle stuck where the subject WAS.
    //   • noDeblock=true: no deblocking filter → sharp block edges (critical).
    //   • partitionMode=0: 16×16 MBs only → large square blocks, not 4×4.
    //   • meRange=22-24: encoder searches widely for skip MBs in static areas.
    //
    // Result: hard, perfectly square, flat-coloured frozen blocks that trail
    //         behind moving subjects. Background pixelates briefly at correct
    //         colours then recovers. No global frame movement at all.
    // =========================================================================

    case 71: { // ── Block Trail Echoes 1 ──────────────────────────────────────
        // Lightest: blockFlatten=80 (partial flatten), qpDelta=51 (locks flat
        // blocks in). 7-frame hard-freeze cascade. Seeds every 10 frames →
        // 3-frame clean recovery. Subtle pixelated trail at motion edges.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        // ghostBlend=0, blockFlatten=80, qpDelta=51, cascadeLen=7, cascadeDecay=0
        plantSeeds(edits, 10, makeSeed(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       51, 0, 0, 0, 80, 0, 0, 7, 0));
        break;
    }

    case 72: { // ── Block Trail Echoes 2 ──────────────────────────────────────
        // Light-medium: blockFlatten=90 (nearly full uniform colour), qpDelta=51.
        // 10-frame hard-freeze cascade. Seeds every 13 frames → 3-frame gap.
        // Slightly harder flat blocks than BTE-1; trails are more rectangular.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        // ghostBlend=0, blockFlatten=90, qpDelta=51, cascadeLen=10, cascadeDecay=0
        plantSeeds(edits, 13, makeSeed(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       51, 0, 0, 0, 90, 0, 0, 10, 0));
        break;
    }

    case 73: { // ── Block Trail Echoes 3 ──────────────────────────────────────
        // Medium: full blockFlatten=100 — every seed-frame MB becomes a perfect
        // flat uniform colour. qpDelta=51. 14-frame hard freeze. Seeds every 17
        // frames → 3-frame gap. meRange=24 maximises encoder freedom for static
        // skip. Subject motion leaves hard flat-colour rectangular mosaic trail.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        // ghostBlend=0, blockFlatten=100, qpDelta=51, cascadeLen=14, cascadeDecay=0
        plantSeeds(edits, 17, makeSeed(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       51, 0, 0, 0, 100, 0, 0, 14, 0));
        break;
    }

    case 74: { // ── Block Trail Echoes 4 ──────────────────────────────────────
        // Heavy: full blockFlatten=100, qpDelta=51. 18-frame hard-freeze cascade.
        // Seeds every 22 frames → 4-frame clean gap. Thick, persistent mosaic
        // blocks frozen at subject's position for nearly a full second at 24fps.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        // ghostBlend=0, blockFlatten=100, qpDelta=51, cascadeLen=18, cascadeDecay=0
        plantSeeds(edits, 22, makeSeed(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       51, 0, 0, 0, 100, 0, 0, 18, 0));
        break;
    }

    case 75: { // ── Block Trail Echoes 5 ──────────────────────────────────────
        // Maximum: full blockFlatten=100, qpDelta=51. 23-frame hard-freeze cascade.
        // Seeds every 27 frames → 4-frame clean gap. Longest sustained flat-block
        // mosaic trail. Hard, sticky, perfectly square frozen rectangles clustered
        // entirely at subject motion. The strongest motion-isolated mosaic preset.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 16;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        // ghostBlend=0, blockFlatten=100, qpDelta=51, cascadeLen=23, cascadeDecay=0
        plantSeeds(edits, 27, makeSeed(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                       51, 0, 0, 0, 100, 0, 0, 23, 0));
        break;
    }

    // Cases 51-70: space-themed, motion-isolated smear presets.
    // KEY DESIGN: meRange=20-24 + cascadeDecay>0 + low mvAmplify(1-2) + no
    // stampAll. Wide meRange lets the encoder find correct predictions in
    // static areas (skip MBs) so the cascade's qpDelta=51 only forces coarse
    // blocks where the encoder fails to find a clean reference — i.e. motion.
    // =========================================================================

    case 51: { // ── Nebula Drift ──────────────────────────────────────────────
        // Seeds every 4 frames. 8-frame cascade, cascadeDecay=12 means ghostBlend
        // fades gently. meRange=22 + mvAmplify=1: static areas skip cleanly,
        // only motion zones accumulate soft trailing block residue.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 4, makeSeed(2, 0, 20, 10, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 8, 12));
        break;
    }

    case 52: { // ── Solar Flare ───────────────────────────────────────────────
        // Seeds every 15 frames, 10-frame cascade, cascadeDecay=8. Strong
        // horizontal displacement seeds the burst; static areas recover fully
        // between events while motion zones remain smeared through the cascade.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 15, makeSeed(2, 0, 35, 0, 2, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 10, 8));
        break;
    }

    case 53: { // ── Asteroid Belt ─────────────────────────────────────────────
        // Very frequent seeds every 2 frames, 2-frame cascade, cascadeDecay=18.
        // meRange=24 + mvAmplify=1 = maximum static skip. Diagonal 18/-12px
        // displacement. Only moving subjects collect any visible smear at all.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 2, makeSeed(1, 0, 18, -12, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 2, 18));
        break;
    }

    case 54: { // ── Pulsar ────────────────────────────────────────────────────
        // Seeds every 8 frames, 5-frame cascade, cascadeDecay=20. High decay
        // means each pulse clears quickly. Static areas skip; motion zones get
        // a brief block smear that fully restores before the next pulse fires.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 8, makeSeed(2, 0, 30, 0, 2, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 5, 20));
        break;
    }

    case 55: { // ── Black Hole ────────────────────────────────────────────────
        // Circular sin/cos drift traces 2 full rotations at 25px amplitude.
        // Seeds every 6 frames, 6-frame cascade, cascadeDecay=10. meRange=22
        // confines smear to subjects; background stays mostly correct.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 6;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(25.0f * sinf(t * 4.0f * 3.14159265f));
                int dy = (int)(25.0f * cosf(t * 4.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, dx, dy, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 6, 10);
            }
        }
        break;
    }

    case 56: { // ── Cosmic Ray ────────────────────────────────────────────────
        // Seeds every 3 frames, 3-frame cascade, cascadeDecay=25. 30/20px
        // diagonal at 2x. High decay = brief streak on motion then gone.
        // meRange=20 keeps block smear from spreading into static zones.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 3, makeSeed(1, 0, 30, 20, 2, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 3, 25));
        break;
    }

    case 57: { // ── Supernova ─────────────────────────────────────────────────
        // Seeds every 40 frames, 15-frame cascade, cascadeDecay=5. Large 40/30px
        // diagonal burst at 2x fully fades before the next event. Motion areas
        // bear the full impact; static zones recover within the 15-frame window.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 40, makeSeed(2, 0, 40, 30, 2, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 15, 5));
        break;
    }

    case 58: { // ── Dark Matter ───────────────────────────────────────────────
        // Seeds every 5 frames, 4-frame cascade, cascadeDecay=30. meRange=24,
        // mvAmplify=1, only 8/5px drift. Virtually invisible in static areas;
        // barely-there block haze clings to motion zones only.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 5, makeSeed(2, 0, 8, 5, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 4, 30));
        break;
    }

    case 59: { // ── Quasar ────────────────────────────────────────────────────
        // Horizontal drift follows 2 sine cycles across the clip. Seeds every
        // 5 frames, 6-frame cascade, cascadeDecay=10. meRange=22. Smear direction
        // reverses mid-clip but stays confined to motion subjects throughout.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(30.0f * sinf(t * 4.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, dx, 0, 2, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 6, 10);
            }
        }
        break;
    }

    case 60: { // ── Event Horizon ─────────────────────────────────────────────
        // Circular drift traces 3 full rotations at 35px amplitude. Seeds every
        // 5 frames, 5-frame cascade, cascadeDecay=8. meRange=22 confines circular
        // block smear to moving subjects; static zones skip correctly.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(35.0f * sinf(t * 6.0f * 3.14159265f));
                int dy = (int)(35.0f * cosf(t * 6.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, dx, dy, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 5, 8);
            }
        }
        break;
    }

    case 61: { // ── Comet Trail ───────────────────────────────────────────────
        // Seeds every 12 frames, 8-frame cascade, cascadeDecay=6. 40px right /
        // 15px up at 2x. The smear trail fans diagonally from motion and fades
        // fully before the next seed fires. Background untouched.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 12, makeSeed(2, 0, 40, -15, 2, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 8, 6));
        break;
    }

    case 62: { // ── Stellar Wind ──────────────────────────────────────────────
        // Seeds every 7 frames, 6-frame cascade, cascadeDecay=15. 22px horizontal
        // at 1x amplification. A soft rightward current smears only motion subjects
        // while static background skips cleanly through every cascade frame.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 7, makeSeed(2, 0, 22, 0, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 6, 15));
        break;
    }

    case 63: { // ── Aurora Borealis ───────────────────────────────────────────
        // Vertical drift follows 2 sine cycles. chromaDriftX=15 + colorTwistU=-40
        // / colorTwistV=+50 on seed frames adds a colour wash to motion blocks.
        // Seeds every 8 frames, 5-frame cascade, cascadeDecay=12. meRange=22.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 8;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dy = (int)(25.0f * sinf(t * 4.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, 0, dy, 1, 0, 0, 0, 15, 0, 0,
                                    0, 0, 0, 0, 0, -40, 50, 5, 12);
            }
        }
        break;
    }

    case 64: { // ── Magnetar ──────────────────────────────────────────────────
        // Seeds every 20 frames, 6-frame cascade, cascadeDecay=20. Strong 50/35px
        // diagonal at 2x. meRange=24 = maximum encoder freedom in static areas.
        // Despite the strong seed, the wide meRange keeps smear at motion only.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 20, makeSeed(3, 0, 50, 35, 2, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 6, 20));
        break;
    }

    case 65: { // ── Wormhole ──────────────────────────────────────────────────
        // Seeds every 4 frames alternate between +35px and -35px horizontal.
        // 3-frame cascade, cascadeDecay=20. Opposing smear directions cancel in
        // static areas but linger as chaotic block trails on moving subjects.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 4;
            int idx = 0;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                int dx = (idx % 2 == 0) ? 35 : -35;
                edits[f] = makeSeed(2, 0, dx, 0, 2, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 3, 20);
            }
        }
        break;
    }

    case 66: { // ── Plasma Stream ─────────────────────────────────────────────
        // Seeds every 3 frames, 4-frame cascade, cascadeDecay=20. 25/15px
        // diagonal at 1x. A continuous low-level diagonal stream that the encoder
        // skips in static zones; visible only where subjects are in motion.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 3, makeSeed(2, 0, 25, 15, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 4, 20));
        break;
    }

    case 67: { // ── Gravity Well ──────────────────────────────────────────────
        // Converging spiral: radius shrinks from 40px to 5px across the clip
        // while completing 3 full rotations. Seeds every 5 frames, 5-frame
        // cascade, cascadeDecay=10. Block smear winds inward on motion subjects.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 22; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 5;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                float r = 40.0f - 35.0f * t;
                int dx = (int)(r * sinf(t * 6.0f * 3.14159265f));
                int dy = (int)(r * cosf(t * 6.0f * 3.14159265f));
                edits[f] = makeSeed(2, 0, dx, dy, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 5, 10);
            }
        }
        break;
    }

    case 68: { // ── Cosmic Web ────────────────────────────────────────────────
        // 5 rapid circular rotations at tiny 15px amplitude. Seeds every 4 frames,
        // 3-frame cascade, cascadeDecay=25, meRange=24. Barely-there circular
        // smear appears as a delicate web of block drift only at moving edges.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 4;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                int dx = (int)(15.0f * sinf(t * 10.0f * 3.14159265f));
                int dy = (int)(15.0f * cosf(t * 10.0f * 3.14159265f));
                edits[f] = makeSeed(1, 0, dx, dy, 1, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 3, 25);
            }
        }
        break;
    }

    case 69: { // ── Stardust ──────────────────────────────────────────────────
        // Seeds every 2 frames, 2-frame cascade, cascadeDecay=35. 10/8px diagonal
        // at 1x. meRange=24 maximum. Nothing visible in static zones; a faint
        // block haze clings to motion subjects and nowhere else.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 24; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        plantSeeds(edits, 2, makeSeed(1, 0, 10, 8, 1, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 2, 35));
        break;
    }

    case 70: { // ── Galaxy Collision ──────────────────────────────────────────
        // Even seeds smear left, odd seeds smear right; both modulated by a sine
        // amplitude envelope so the force peaks at mid-clip then tapers. Seeds
        // every 4 frames, 5-frame cascade, cascadeDecay=8. meRange=20.
        // Opposing currents cancel in static areas, collide at moving subjects.
        gp.gopSize = 0; gp.killIFrames = true; gp.bFrames = 0; gp.refFrames = 8;
        gp.noDeblock = true; gp.partitionMode = 0; gp.subpelRef = 1; gp.meMethod = 1;
        gp.meRange = 20; gp.trellis = 0; gp.use8x8DCT = false;
        gp.mbTreeDisable = true; gp.psyRD = 0.0f; gp.aqMode = 0;
        {
            const int interval = 4;
            int idx = 0;
            const int count = (total - seedFrame + interval - 1) / interval;
            for (int f = seedFrame; f < total; f += interval, ++idx) {
                float t = count > 1 ? (float)idx / (float)(count - 1) : 0.0f;
                float envelope = sinf(t * 3.14159265f); // peaks at mid-clip
                int dx = (int)(40.0f * envelope * ((idx % 2 == 0) ? 1.0f : -1.0f));
                edits[f] = makeSeed(2, 0, dx, 0, 2, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 5, 8);
            }
        }
        break;
    }

    default:
        return;
    }

    m_mbWidget->loadEditMap(edits);
    startTransform(FrameTransformerWorker::MBEditOnly, gp);
}
#endif // ── end of removed onQuickMosh ────────────────────────────────────────

// =============================================================================
// Quick Mosh — user preset save / load
// =============================================================================

void MainWindow::onQuickMoshSaveUserPreset()
{
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, "No Video",
            "Open a video first before saving a Quick Mosh preset.");
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Save Quick Mosh Preset",
        "Preset name\n(captures current MB Editor controls + Global Encode Params):",
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    const FrameMBParams mb = m_mbWidget->currentControlParams();
    const GlobalEncodeParams gp = m_globalParams->currentParams();

    if (!PresetManager::saveQuickMosh(name, mb, gp)) {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not save Quick Mosh preset \"%1\".").arg(name));
        return;
    }
    m_quickMosh->refreshUserPresets();
}

void MainWindow::onQuickMoshUserMosh(const QString& presetName)
{
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;
    if (m_lastAnalysis.frames.isEmpty()) return;

    FrameMBParams mb;
    GlobalEncodeParams gp;
    if (!PresetManager::loadQuickMosh(presetName, mb, gp)) {
        QMessageBox::warning(this, "Load Failed",
            QString("Could not load Quick Mosh preset \"%1\".").arg(presetName));
        return;
    }

    // Apply the loaded MB params to every frame, spread across the whole clip.
    const int total = m_lastAnalysis.frames.size();
    MBEditMap edits;
    for (int i = 0; i < total; ++i) edits[i] = mb;

    m_mbWidget->loadEditMap(edits);
    startTransform(FrameTransformerWorker::MBEditOnly, gp);
}

// =============================================================================
// onFrameReorderRequested — called when the user drag-drops a frame in the
// timeline.  Builds a ReorderFrames worker directly (bypasses startTransform
// because the frame-index payload has a different meaning here).
// =============================================================================

void MainWindow::onFrameReorderRequested(int sourceIdx, int insertBeforeIdx)
{
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;
    if (m_lastAnalysis.frames.isEmpty()) return;

    // Sanity bounds
    const int total = m_lastAnalysis.frames.size();
    if (sourceIdx < 0 || sourceIdx >= total) return;
    insertBeforeIdx = qBound(0, insertBeforeIdx, total);

    // No-op: dropped at its own position or immediately after
    if (insertBeforeIdx == sourceIdx || insertBeforeIdx == sourceIdx + 1) return;

    // Non-destructive: no shadow backup needed.  The render writes a new
    // .vNN.mp4 sibling; the source stays on disk.  Ctrl+Z walks the version
    // chain backwards via VersionPathUtil::previousVersionPath.

    m_preview->unloadVideo();
    m_transformBusy = true;
    setTransformButtonsEnabled(false);
    m_progressPanel->setProgressVisible(true);
    m_progressPanel->setRange(0, total);
    m_progressPanel->setValue(0);
    statusBar()->showMessage(
        QString("Reordering: moving frame %1 to position %2 ...")
            .arg(sourceIdx).arg(insertBeforeIdx));

    auto* worker = new FrameTransformerWorker(
        m_currentVideoPath,
        QVector<int>{ sourceIdx, insertBeforeIdx },
        FrameTransformerWorker::ReorderFrames,
        total,
        QVector<char>(), MBEditMap(), GlobalEncodeParams(), 1, nullptr);

    auto* thread = new QThread(this);
    worker->moveToThread(thread);
    connect(thread, &QThread::started,  worker, &FrameTransformerWorker::run);
    connect(worker, &FrameTransformerWorker::progress,
            this,   &MainWindow::onTransformProgress);
    connect(worker, &FrameTransformerWorker::done,
            this,   &MainWindow::onTransformDone);
    connect(worker, &FrameTransformerWorker::done,  thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// =============================================================================
// startTransform
// =============================================================================

void MainWindow::startTransform(FrameTransformerWorker::TargetType type,
                                const GlobalEncodeParams& globalParams,
                                int interpolateCount)
{
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;

    // Capture render type for the post-render reload logic — see the comment
    // on m_lastRenderType in MainWindow.h.
    m_lastRenderType = type;

    QVector<int> sel = m_timeline->selectedFrames();

    if (type == FrameTransformerWorker::MBEditOnly) {
        bool hasGlobalChange =
            (globalParams.gopSize != -1 || globalParams.killIFrames ||
             globalParams.bFrames != -1 || globalParams.refFrames != -1 ||
             globalParams.qpOverride != -1 || globalParams.qpMin != -1 ||
             globalParams.qpMax != -1 || globalParams.meMethod != -1 ||
             globalParams.meRange != -1 || globalParams.subpelRef != -1 ||
             globalParams.partitionMode != -1 || globalParams.trellis != -1 ||
             globalParams.noFastPSkip || globalParams.noDctDecimate ||
             globalParams.cabacDisable || globalParams.noDeblock ||
             globalParams.psyRD >= 0.0f || globalParams.aqMode != -1 ||
             globalParams.mbTreeDisable);

        // Hidden panel logic: if MB editor is not visible, treat its edit map
        // as empty so no invisible state can silently corrupt the encode.
        // m_mbWidget itself is a hidden coordinator; check whether either
        // of its visual docks (canvas or editor) is open to decide if the
        // user has the MB editing surface available.
        bool mbVisible = (m_mbCanvasDock && m_mbCanvasDock->isOpen())
                      || (m_mbEditorDock && m_mbEditorDock->isOpen());
        if (!mbVisible && !hasGlobalChange) {
            statusBar()->showMessage(
                "Nothing to apply — MB Editor is hidden and no global changes are set.");
            return;
        }
        if (mbVisible && m_mbWidget->editMap().isEmpty() && !hasGlobalChange) {
            statusBar()->showMessage(
                "Nothing to apply — paint MBs or adjust Global Encode Params first.");
            return;
        }
    } else {
        if (sel.isEmpty()) return;
    }

    // Non-destructive: no shadow backup needed — each render writes a new
    // .vNN.mp4 sibling (see FrameTransformerWorker::commitVersionedOutput)
    // and the source file stays intact.  Ctrl+Z walks backwards through the
    // version chain via VersionPathUtil::previousVersionPath.

    m_preview->unloadVideo();
    m_transformBusy = true;
    setTransformButtonsEnabled(false);
    m_progressPanel->setProgressVisible(true);
    m_progressPanel->setRange(0, m_lastAnalysis.frames.size());
    m_progressPanel->setValue(0);

    if (type == FrameTransformerWorker::MBEditOnly) {
        // Detect whether any frame carries a bitstream-surgery knob — the
        // FrameTransformer routes those through runBitstreamEdit() (our direct
        // libx264 path), not the FFmpeg pixel-domain path.  Surface which
        // engine is running so the user can correlate behaviour with the
        // rendering mode.
        bool anyBS = false;
        if (m_mbWidget->isVisible()) {
            const MBEditMap& em = m_mbWidget->editMap();
            for (auto it = em.constBegin(); it != em.constEnd() && !anyBS; ++it) {
                const FrameMBParams& p = it.value();
                /* bsMbType intentionally dropped here — feature migrated to
                 * GlobalEncodeParams::partitionMode.  The per-MB field is no
                 * longer set by any UI; checking it would be dead code. */
                if (p.bsCbpZero > 0 || p.bsCbpZeroLuma > 0 || p.bsCbpZeroChroma > 0 ||
                    p.bsForceSkip > 0 ||
                    p.bsIntraMode >= 0 ||
                    p.bsMvdX != 0 || p.bsMvdY != 0 ||
                    p.bsDctScale != 100) anyBS = true;
            }
        }
        statusBar()->showMessage(
            QString("%1 — applying MB edits to %2 frame%3 ...")
                .arg(anyBS ? "Bitstream surgery (direct libx264)" : "Pixel-domain (FFmpeg)")
                .arg(m_lastAnalysis.frames.size())
                .arg(m_lastAnalysis.frames.size() == 1 ? "" : "s"));
    } else {
        static const char* names[] = {
            "I","P","B","deleted","dup-left","dup-right","MB edit",
            "interp-left","interp-right"
        };
        const char* typeName = (type < (int)(sizeof(names)/sizeof(names[0])))
                               ? names[type] : "transform";
        statusBar()->showMessage(
            QString("Processing %1 frame%2 → %3 ...")
                .arg(sel.size()).arg(sel.size() == 1 ? "" : "s").arg(typeName));
    }

    QVector<char> origTypes;
    origTypes.reserve(m_lastAnalysis.frames.size());
    for (const FrameInfo& f : m_lastAnalysis.frames)
        origTypes.append(f.pictType);

    // Only send MB edits when the MB editor is visible (hidden-panel logic).
    // Only apply MB edits when the user explicitly clicks RENDER (MBEditOnly).
    // Structural operations (Force I/P/B, Delete, Dup, Interp, Flip/Flop)
    // should NOT bake in pending MB edits — the user may still be mid-edit
    // and expects the type change to be an isolated operation.
    MBEditMap edits = (type == FrameTransformerWorker::MBEditOnly)
                      ? m_mbWidget->editMap()
                      : MBEditMap{};

    auto* worker = new FrameTransformerWorker(
        m_currentVideoPath, sel, type,
        m_lastAnalysis.frames.size(), origTypes,
        edits, globalParams, interpolateCount, nullptr);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,  worker, &FrameTransformerWorker::run);
    connect(worker, &FrameTransformerWorker::progress,
            this,   &MainWindow::onTransformProgress);
    connect(worker, &FrameTransformerWorker::warning,
            this,   [this](const QString& msg){ statusBar()->showMessage("\u26a0 " + msg); });
    connect(worker, &FrameTransformerWorker::done,
            this,   &MainWindow::onTransformDone);
    connect(worker, &FrameTransformerWorker::done,  thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

// =============================================================================
// Transform progress / completion
// =============================================================================

void MainWindow::onTransformProgress(int current, int /*total*/)
{
    m_progressPanel->setValue(current);
}

void MainWindow::onTransformDone(bool success, QString errorMessage, QString outputPath)
{
    m_transformBusy = false;
    m_progressPanel->setProgressVisible(false);

    if (!success) {
        statusBar()->showMessage("Transform failed: " + errorMessage);
        QMessageBox::critical(this, "Transform Failed", errorMessage);
        // Non-destructive renders can't corrupt the source, so there is nothing
        // to roll back on failure — the source is untouched on disk.  Previous
        // versions the user may have rendered remain in the bin.
        return;
    }

    // Swap the active video to the fresh iteration, preserving the source.
    // Empty outputPath means the render was a no-op (e.g. reorder with
    // source == destination) — in that case we just stay on the current file.
    if (!outputPath.isEmpty())
        m_currentVideoPath = outputPath;

    // Generate thumbnail for the new render — best effort, non-blocking of UX
    // if it fails (e.g. decode hiccup; next refresh shows a placeholder).
    if (!outputPath.isEmpty() && m_project) {
        QString thumbErr;
        ThumbnailGenerator::generateMidFrameThumbnail(
            outputPath,
            m_project->thumbnailPathFor(outputPath),
            thumbErr);
    }

    statusBar()->showMessage("Transform complete — reloading...");
    reloadVideoAndTimeline();

    // Stamp the project's active video so next launch resumes here.
    if (m_project && !outputPath.isEmpty()) {
        m_project->setActiveVideo(m_currentVideoPath);
        QString saveErr; m_project->save(saveErr);
    }

    // Refresh the Media Bin so the new .vNN.mp4 appears under its source's
    // expandable group, and highlight the freshly-active iteration so the
    // user can see where they just landed in the version chain.
    if (m_mediaBin) {
        m_mediaBin->refresh();
        m_mediaBin->setCurrentVideoPath(m_currentVideoPath);
    }
}

// =============================================================================
// Ctrl+Z — load previous version
// =============================================================================
//
// Replaces the former shadow-file undo.  Walks VersionPathUtil to find the
// nearest lower version in the current video's family on disk and loads it.
// No-op if the current video is the root import (no earlier version to
// step back to).

// =============================================================================
// Project lifecycle
// =============================================================================

QString MainWindow::defaultProjectsRoot()
{
    // ~/Documents/LaMoshPit Projects/ — the Windows convention for app data
    // that the user expects to manage themselves (backups, shares, etc.).
    // Created on first access so the folder always exists before any File
    // dialog points at it.
    const QString docs = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    const QString root = docs + "/LaMoshPit Projects";
    QDir().mkpath(root);
    return root;
}

void MainWindow::setActiveProject(std::unique_ptr<Project> p)
{
    if (!p) return;

    // Stash the outgoing clip's in-flight edits INTO the outgoing project
    // before we replace it — keeps "unsaved state on project A" alive when
    // the user bounces over to project B and back.  Caller decides whether
    // to actually save; we only guarantee the in-memory capture so the
    // data isn't dropped on the floor during project switch.
    if (m_project && !m_currentVideoPath.isEmpty()) {
        const QString key = m_project->compressToTokens(m_currentVideoPath);
        m_project->setClipEditJson(key, captureCurrentClipEdits());
    }

    // Clear the sequencer too — its tracks/clips belong to the outgoing
    // project and shouldn't leak into the new one.  Serialisation (Phase 6)
    // will restore the incoming project's sequencer state.
    if (m_seqProject) m_seqProject->clear();

    // Clear the undo stack — commands hold raw pointers to widgets and
    // captured state that belongs to the outgoing project.  Applying them
    // to the incoming project's widget content would be nonsensical.
    if (m_undoController) m_undoController->clear();

    // Remember the new project's folder for next-launch resume.
    QSettings settings("LaMoshPit", "LaMoshPit");
    settings.setValue("project/lastOpened", p->folderPath());

    // Tear down current video state — about to re-point at a different
    // MoshVideoFolder and the currently-loaded video almost certainly
    // isn't in the new project.
    m_preview->unloadVideo();
    m_currentVideoPath.clear();
    m_lastAnalysis = AnalysisReport();

    // Clear the MB grid canvas synchronously so the outgoing project's last
    // frame + painted selection don't linger while the new project either
    // resolves its active video (async analyze) or settles on "no video".
    if (m_mbWidget) m_mbWidget->setVideo(QString(), AnalysisReport{});

    m_project = std::move(p);
    connect(m_project.get(), &Project::dirtyChanged,
            this, [this](bool){ updateWindowTitle(); });
    updateWindowTitle();

    if (m_mbWidget) {
        m_mbWidget->setProjectPaths(m_project->moshVideoFolder(),
                                    m_project->selectionMapsDir());
    }

    // Re-root the bin on the new project's MoshVideoFolder, then load the
    // project's remembered active video if it still exists on disk.
    // MediaBinWidget is constructed with a fixed root, so we swap the inner
    // widget of the dock for a fresh instance pointed at the new project.
    if (m_mediaBin && m_mediaBinDock) {
        auto* oldBin = m_mediaBin;
        m_mediaBin = new MediaBinWidget(m_project->moshVideoFolder(),
                                        m_project->thumbnailsDir(),
                                        nullptr);
        connect(m_mediaBin, &MediaBinWidget::videoSelected,
                this, &MainWindow::onMediaBinVideoSelected);
        connect(m_mediaBin, &MediaBinWidget::importRequested,
                this, &MainWindow::openFile);
        m_mediaBinDock->setWidget(m_mediaBin);
        oldBin->deleteLater();
    }

    // Restore the sequencer + router state saved with the project, if any.
    // Missing sections (v1 projects, or freshly-created v2 projects that
    // have never held sequencer content) no-op — the subsystems keep their
    // post-clear() defaults.  Restoring BEFORE resumePath so the sequencer
    // is coherent when the user sees the dock's first frame.
    if (m_seqProject)
        m_seqProject->fromJson(m_project->sequencerStateJson(), *m_project);
    if (m_seqDock)
        m_seqDock->routerConfigFromJson(m_project->frameRouterStateJson());

    const QString resumePath = m_project->activeVideo();
    if (!resumePath.isEmpty() && QFile::exists(resumePath)) {
        switchActiveClip(resumePath);
    } else {
        // No active video or it's gone — clear the timeline / MB editor.
        populateTimeline(AnalysisReport{});
    }

    // Project just loaded from disk — state and disk are in sync.  Capture/
    // restore calls on the way here may have flipped the dirty flag via
    // markDirty(); reset so the title starts clean.
    m_project->clearDirty();
    updateWindowTitle();

    statusBar()->showMessage("Project: " + m_project->name());
}

void MainWindow::onNewProject()
{
    // Ask where to create.  Default parent = ~/Documents/LaMoshPit Projects/.
    // The user picks an empty / new directory; we create it if needed.
    const QString pick = QFileDialog::getSaveFileName(this,
        "New Project \u2014 choose folder name",
        defaultProjectsRoot() + "/Untitled",
        "LaMoshPit Project (*)",
        nullptr, QFileDialog::DontConfirmOverwrite);
    if (pick.isEmpty()) return;

    const QString name = QFileInfo(pick).fileName();
    QString err;
    auto project = Project::create(pick, name, err);
    if (!project) {
        QMessageBox::warning(this, "New Project Failed", err);
        return;
    }
    setActiveProject(std::move(project));
}

void MainWindow::onOpenProject()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
        "Open Project \u2014 pick folder",
        defaultProjectsRoot(),
        QFileDialog::ShowDirsOnly);
    if (folder.isEmpty()) return;

    QString err;
    auto project = Project::open(folder, err);
    if (!project) {
        QMessageBox::warning(this, "Open Project Failed", err);
        return;
    }
    setActiveProject(std::move(project));
}

bool MainWindow::saveActiveProject(QString& errorMsg)
{
    if (!m_project) { errorMsg = QStringLiteral("No active project"); return false; }

    // Snapshot the current active video so next open restores it.
    m_project->setActiveVideo(m_currentVideoPath);

    // Capture in-flight state from the live subsystems into Project so
    // save() picks it up:
    //   - Current clip's MB edits + Global Encode Params knobs
    //   - SequencerProject tracks / clips / loop / FPS / active track
    //   - FrameRouter transition + hotkey config
    if (!m_currentVideoPath.isEmpty()) {
        const QString key = m_project->compressToTokens(m_currentVideoPath);
        m_project->setClipEditJson(key, captureCurrentClipEdits());
    }
    if (m_seqProject)
        m_project->setSequencerStateJson(m_seqProject->toJson(*m_project));
    if (m_seqDock)
        m_project->setFrameRouterStateJson(m_seqDock->routerConfigToJson());

    return m_project->save(errorMsg);
}

void MainWindow::onSaveProject()
{
    if (!m_project) return;
    QString err;
    if (saveActiveProject(err))
        statusBar()->showMessage("Project saved: " + m_project->name());
    else
        QMessageBox::warning(this, "Save Failed", err);
}

// =============================================================================
// File → Import Selection Map
// =============================================================================
void MainWindow::onImportSelectionMap()
{
    if (!m_project) {
        QMessageBox::information(this, "Import Selection Map",
            "Open or create a project before importing a selection map.");
        return;
    }
    // No pre-selected clip when invoked from the File menu — the user
    // picks both the map and the target clip inside the dialog.
    ImportSelectionMapDialog dlg(m_project->moshVideoFolder(),
                                 m_project->selectionMapsDir(),
                                 QString(), this);
    if (dlg.exec() == QDialog::Accepted) {
        statusBar()->showMessage(
            "Imported selection map for: " +
            QFileInfo(dlg.associatedClipPath()).fileName());
        // If the map was associated with the currently-loaded clip, the
        // MacroblockWidget's Apply Map dialog will pick it up on next open.
    }
}

// =============================================================================
// NLE render handler
// =============================================================================
//
// Dock-driven flow:
//   1. User opens the SequencerRenderDialog, picks track + range + encoder.
//   2. Dock emits renderRequested(params, importIntoProject).
//   3. We land here — fill in globalParams from the live mosh-editor panel
//      if the user chose "Libx264FromGlobal", spawn the worker on a
//      QThread, drive the main progress bar, and on completion handle the
//      optional import-back to the project's MoshVideoFolder/ folder.
//
// Runs in parallel with everything else — users can keep editing the
// timeline while a render is in progress; the worker snapshots the Params
// at spawn time so mid-render edits don't corrupt the output.

void MainWindow::onNleRenderRequested(const sequencer::SequencerRenderer::Params& incoming,
                                      bool importIntoProject)
{
    if (!m_seqProject) return;
    if (m_transformBusy) {
        statusBar()->showMessage(
            "A transform is already running \u2014 wait for it to finish.");
        return;
    }

    // Copy the dialog's params so we can patch in GlobalEncodeParams when
    // the user chose the "Use current Global Encode Params" option.
    sequencer::SequencerRenderer::Params params = incoming;
    if (params.encoderMode
        == sequencer::SequencerRenderer::EncoderMode::Libx264FromGlobal
        && m_globalParams) {
        params.globalParams = m_globalParams->currentParams();
    }

    auto* worker = new sequencer::SequencerRenderer(m_seqProject, params);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    m_transformBusy = true;
    if (m_progressPanel) {
        m_progressPanel->setProgressVisible(true);
        m_progressPanel->setRange(0, 100);
        m_progressPanel->setValue(0);
    }
    statusBar()->showMessage("Rendering NLE sequence\u2026");

    connect(thread, &QThread::started, worker,
            &sequencer::SequencerRenderer::run);
    connect(worker, &sequencer::SequencerRenderer::progress, this,
            [this](int cur, int total) {
        if (total > 0 && m_progressPanel) {
            m_progressPanel->setRange(0, total);
            m_progressPanel->setValue(cur);
        }
    });

    // Completion — handle import-back + thumbnail + bin refresh on the GUI
    // thread (this lambda runs on MainWindow's thread because `this` is the
    // receiver context).
    connect(worker, &sequencer::SequencerRenderer::done, this,
            [this, importIntoProject]
            (bool success, QString errMsg, QString outPath) {
        m_transformBusy = false;
        if (m_progressPanel) m_progressPanel->setProgressVisible(false);
        if (!success) {
            statusBar()->showMessage("NLE render failed: " + errMsg);
            QMessageBox::critical(this, "Render Failed", errMsg);
            return;
        }

        // If the output landed outside the project and the user asked for
        // import-back, copy it into MoshVideoFolder/.  The file is already
        // clean H.264/MP4; no re-transcode needed.
        QString inProjectPath;
        if (importIntoProject && m_project) {
            const QString importsDir = m_project->moshVideoFolder();
            const QString outDir     = QFileInfo(outPath).absolutePath();
            if (QDir(outDir).absolutePath() != QDir(importsDir).absolutePath()) {
                const QString base  = QFileInfo(outPath).completeBaseName();
                const QString dest  = QDir(importsDir).absoluteFilePath(
                    base + "_imported.mp4");
                QFile::remove(dest);
                if (QFile::copy(outPath, dest)) {
                    inProjectPath = dest;
                    statusBar()->showMessage(
                        "NLE render complete \u2014 imported as "
                        + QFileInfo(dest).fileName());
                } else {
                    statusBar()->showMessage(
                        "NLE render complete, but import-back copy failed.");
                }
            } else {
                inProjectPath = outPath;
                statusBar()->showMessage(
                    "NLE render complete \u2014 "
                    + QFileInfo(outPath).fileName());
            }
        } else {
            statusBar()->showMessage(
                "NLE render complete \u2014 "
                + QFileInfo(outPath).fileName());
        }

        // Thumbnail + Media Bin refresh only when the file landed inside
        // the project — ThumbnailGenerator writes into the project's
        // thumbnails/ folder keyed by the imported filename.
        if (!inProjectPath.isEmpty() && m_project) {
            QString thumbErr;
            ThumbnailGenerator::generateMidFrameThumbnail(
                inProjectPath,
                m_project->thumbnailPathFor(inProjectPath),
                thumbErr);
        }
        if (m_mediaBin) m_mediaBin->refresh();
    });

    connect(worker, &sequencer::SequencerRenderer::done,
            thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// =============================================================================

void MainWindow::onLoadPreviousVersion()
{
    if (m_currentVideoPath.isEmpty()) return;
    const QString prev = VersionPathUtil::previousVersionPath(m_currentVideoPath);
    if (prev.isEmpty()) {
        statusBar()->showMessage("No earlier version in this video's family.");
        return;
    }
    m_currentVideoPath = prev;
    statusBar()->showMessage("Loaded previous version: " + QFileInfo(prev).fileName());
    reloadVideoAndTimeline();
    if (m_mediaBin) m_mediaBin->setCurrentVideoPath(m_currentVideoPath);
}

// =============================================================================
// Media Bin — video selected from the tree
// =============================================================================
//
// The bin emits this when the user double-clicks or picks "Load" from the
// context menu.  Videos are already in MoshVideoFolder/ and have been
// analyzable since their creation, so we skip the DecodePipeline transcode
// that openFile() would run and go straight to the load + analyze pipeline
// that openFile()'s completion callback uses.  A bin selection while a
// render is in progress is ignored to avoid tearing down state mid-pipeline.

void MainWindow::onMediaBinVideoSelected(const QString& videoPath)
{
    if (m_transformBusy) {
        statusBar()->showMessage("Render in progress — ignoring bin selection.");
        return;
    }
    statusBar()->showMessage("Loading: " + QFileInfo(videoPath).fileName());
    switchActiveClip(videoPath);
}

// =============================================================================
// Reload after transform / undo
// =============================================================================

void MainWindow::reloadVideoAndTimeline()
{
    // Breadcrumb trace — each line prints BEFORE the step runs, so the last
    // line in the log file identifies the step that crashed on a hard crash.
    // Routed through ControlLogger so breadcrumbs end up in the same file as
    // the APPLY STARTED / COMPLETED markers.
    auto& L = ControlLogger::instance();

    // MBEditOnly renders do not change the frame count or per-frame slice
    // types — only MB content within each frame.  That means the cached
    // m_lastAnalysis from before the render is still accurate and we can
    // safely skip BitstreamAnalyzer::analyzeVideo() here.  Doing so avoids
    // h264bitstream's fragile NAL parser, which crashes when fed outputs
    // that contain long runs of P_SKIP macroblocks (e.g. the output of a
    // Force-Skip bitstream-surgery render).  Frame-type-changing renders
    // (ForceI/P/B, DeleteFrames, Interp*, Reorder, Flip*, Dup*) fall through
    // to the full re-analysis path since their outputs have different
    // structure than the input.
    const bool canReuseAnalysis =
        (m_lastRenderType == FrameTransformerWorker::MBEditOnly) &&
        m_lastAnalysis.success;

    L.logNote("reloadVideoAndTimeline — step 1: preview->loadVideo()");
    m_preview->loadVideo(m_currentVideoPath);
    L.logNote("reloadVideoAndTimeline — step 2: videoSequence->load()");
    m_videoSequence->load(m_currentVideoPath);

    if (canReuseAnalysis) {
        L.logNote("reloadVideoAndTimeline — step 3: SKIPPED — reusing cached "
                  "analysis for MBEditOnly render (frame structure unchanged)");
    } else {
        L.logNote("reloadVideoAndTimeline — step 3: BitstreamAnalyzer::analyzeVideo()");
        m_lastAnalysis = BitstreamAnalyzer::analyzeVideo(m_currentVideoPath);
        L.logNote(QString("reloadVideoAndTimeline — step 4: analysis.success = %1")
                  .arg(m_lastAnalysis.success ? "true" : "false"));
    }

    if (m_lastAnalysis.success) {
        if (!canReuseAnalysis) {
            L.logNote("reloadVideoAndTimeline — step 5: printAnalysis");
            BitstreamAnalyzer::printAnalysis(m_lastAnalysis);
            L.logNote("reloadVideoAndTimeline — step 6: saveAnalysisToFile");
            BitstreamAnalyzer::saveAnalysisToFile(m_lastAnalysis, m_currentVideoPath);
        }
        L.logNote("reloadVideoAndTimeline — step 7: populateTimeline");
        populateTimeline(m_lastAnalysis);
        L.logNote("reloadVideoAndTimeline — step 8: mbWidget->reload()");
        m_mbWidget->reload(m_currentVideoPath, m_lastAnalysis);
        L.logNote("reloadVideoAndTimeline — step 9: status update");
        statusBar()->showMessage(
            QString("Ready — %1 frames  I:%2  P:%3  B:%4")
                .arg(m_lastAnalysis.totalFrames)
                .arg(m_lastAnalysis.iFrameCount)
                .arg(m_lastAnalysis.pFrameCount)
                .arg(m_lastAnalysis.bFrameCount));
    } else {
        statusBar()->showMessage("Re-analysis failed: " + m_lastAnalysis.errorMessage);
    }
    L.logNote("reloadVideoAndTimeline — DONE");
}

// =============================================================================
// Timeline ↔ MB editor sync
// =============================================================================

void MainWindow::onMBFrameNavigated(int frameIdx)
{
    m_timeline->setSelection(frameIdx);
}

// =============================================================================
// Save
// =============================================================================

void MainWindow::saveHacked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, "Save Mosh Pit", "", "H.264 MP4 Files (*.mp4)");
    if (!fileName.isEmpty()) {
        if (QFile::copy(m_currentVideoPath, fileName))
            statusBar()->showMessage("Saved: " + fileName);
        else
            QMessageBox::warning(this, "Save Failed", "Could not save file.");
    }
}

// =============================================================================
// eventFilter — reserved for future per-child event interception. Float/dock
// state for every panel is now handled by QDockWidget itself.
// =============================================================================
bool MainWindow::eventFilter(QObject* obj, QEvent* e)
{
    return QMainWindow::eventFilter(obj, e);
}

// =============================================================================
// Per-clip edit capture / apply — Scope B implementation of the workflow
// "switch away from clip A, work on clip B, come back to A and all knob /
// selection changes from before are still there".  Serialisation uses the
// same json codecs PresetManager uses for its on-disk presets so the shape
// is reused rather than reinvented.
// =============================================================================
QJsonObject MainWindow::captureCurrentClipEdits() const
{
    QJsonObject state;
    state["version"] = 1;

    QJsonObject mbObj;
    if (m_mbWidget) {
        const MBEditMap& edits = m_mbWidget->editMap();
        for (auto it = edits.begin(); it != edits.end(); ++it) {
            mbObj[QString::number(it.key())] =
                PresetManager::mbToJson(it.value());
        }
    }
    state["mbEdits"] = mbObj;

    if (m_globalParams) {
        state["globalParams"] =
            PresetManager::gpToJson(m_globalParams->currentParams());
    }
    return state;
}

void MainWindow::applyClipEdits(const QJsonObject& state)
{
    if (state.isEmpty()) return;

    if (m_mbWidget) {
        const QJsonObject mbObj = state.value("mbEdits").toObject();
        MBEditMap edits;
        for (auto it = mbObj.begin(); it != mbObj.end(); ++it) {
            bool ok = false;
            const int frameIdx = it.key().toInt(&ok);
            if (!ok) continue;
            edits[frameIdx] = PresetManager::jsonToMB(it.value().toObject());
        }
        m_mbWidget->loadEditMap(edits);
    }

    if (m_globalParams && state.contains("globalParams")) {
        m_globalParams->setParams(
            PresetManager::jsonToGP(state.value("globalParams").toObject()));
    }
}

// =============================================================================
// switchActiveClip — user-initiated clip-swap.  Creates a ClipSwitchCommand
// so Ctrl+Z can walk back through clip swaps, then delegates the mechanical
// work to switchActiveClipDirect (which the command's redo/undo also call).
// First-load scenarios (no outgoing clip, or no controller yet) bypass
// command creation because there's no previous state to undo into.
// =============================================================================
void MainWindow::switchActiveClip(const QString& newPath)
{
    if (newPath.isEmpty() || !QFile::exists(newPath)) return;
    if (newPath == m_currentVideoPath) return;

    if (m_currentVideoPath.isEmpty() || !m_undoController) {
        switchActiveClipDirect(newPath);
        return;
    }

    // Flush any pending debounced MB/GP edits into their own commands
    // BEFORE the clip switch lands on the stack.  Otherwise a knob turn
    // immediately followed by a bin click would collapse into the clip
    // switch command, losing per-edit granularity on undo.
    flushPendingCommits();

    m_undoController->execute(std::make_unique<undo::ClipSwitchCommand>(
        this, m_currentVideoPath, newPath));
}

// =============================================================================
// Scope C undo-debouncer slots
// =============================================================================

void MainWindow::syncLastKnownToWidgets()
{
    if (m_mbWidget)     m_lastKnownMBEdits = m_mbWidget->editMap();
    if (m_globalParams) m_lastKnownGP      = m_globalParams->currentParams();
}

void MainWindow::onMBEditCommitted()
{
    if (m_mbCommitTimer) m_mbCommitTimer->start();
}

void MainWindow::commitMBEditIfChanged()
{
    if (!m_mbWidget || !m_undoController) return;
    MBEditMap live = m_mbWidget->editMap();
    if (live == m_lastKnownMBEdits) return;
    auto cmd = std::make_unique<undo::MBEditMapReplaceCommand>(
        m_mbWidget, m_lastKnownMBEdits, live);
    // Skip the execute's cmd->redo() — widgets are already in the "after"
    // state; just seat the command on the stack.  Emit commandExecuted
    // manually so dirty-tracking / syncLastKnown fire as usual.
    // Easiest implementation: call execute() but our redo() is idempotent
    // (loadEditMap(after) when widgets are already at 'after' is a no-op
    // beyond re-emitting loadKnobsFromCurrentFrame — harmless).
    m_undoController->execute(std::move(cmd));
    // execute()'s commandExecuted signal re-triggers syncLastKnownToWidgets
    // which sets m_lastKnownMBEdits = live.  No extra work needed.
}

void MainWindow::onGPParamsChanged()
{
    if (m_gpCommitTimer) m_gpCommitTimer->start();
}

void MainWindow::commitGPIfChanged()
{
    if (!m_globalParams || !m_undoController) return;
    GlobalEncodeParams live = m_globalParams->currentParams();
    if (live == m_lastKnownGP) return;
    auto cmd = std::make_unique<undo::GlobalParamsReplaceCommand>(
        m_globalParams, m_lastKnownGP, live);
    m_undoController->execute(std::move(cmd));
}

void MainWindow::flushPendingCommits()
{
    if (m_mbCommitTimer && m_mbCommitTimer->isActive()) {
        m_mbCommitTimer->stop();
        commitMBEditIfChanged();
    }
    if (m_gpCommitTimer && m_gpCommitTimer->isActive()) {
        m_gpCommitTimer->stop();
        commitGPIfChanged();
    }
}

// =============================================================================
// switchActiveClipDirect — mechanical clip swap.  Same contract as the old
// switchActiveClip pre-Scope-C: captures outgoing in-flight edits into
// Project, runs the analyze/load pipeline, restores stashed incoming edits.
// Called directly for first-load scenarios and by ClipSwitchCommand on
// every redo/undo.  Does NOT create an undo command — that would recurse.
// =============================================================================
void MainWindow::switchActiveClipDirect(const QString& newPath)
{
    if (newPath.isEmpty() || !QFile::exists(newPath)) return;
    if (newPath == m_currentVideoPath) return;

    // 1. Snapshot the outgoing clip's in-flight edits.
    if (m_project && !m_currentVideoPath.isEmpty()) {
        const QString key = m_project->compressToTokens(m_currentVideoPath);
        m_project->setClipEditJson(key, captureCurrentClipEdits());
    }

    // 2. Standard load — preview / sequence / analyze (which drives the MB
    // editor's setVideo, clearing its in-memory edit map).
    m_currentVideoPath = newPath;
    if (m_preview)       m_preview->loadVideo(newPath);
    if (m_videoSequence) m_videoSequence->load(newPath);
    analyzeImportedVideo(newPath);

    // 3. Re-hydrate any stashed edits for the incoming clip.  If there's
    // nothing stashed, the widgets keep the fresh-state they got from step 2.
    if (m_project) {
        const QString key = m_project->compressToTokens(newPath);
        if (m_project->hasClipEdit(key))
            applyClipEdits(m_project->clipEditJson(key));
    }

    if (m_mediaBin) m_mediaBin->setCurrentVideoPath(newPath);

    // Update the undo-tracking cache so the next user edit diffs against
    // the freshly-loaded state (not leftovers from the outgoing clip).
    syncLastKnownToWidgets();
}

// =============================================================================
// updateWindowTitle — compose from m_project + dirty flag.  Prepends "* "
// when the active project has unsaved changes (Qt/Windows convention).
// =============================================================================
void MainWindow::updateWindowTitle()
{
    const QString name = m_project ? m_project->name() : QStringLiteral("(no project)");
    const bool dirty   = m_project && m_project->isDirty();
    const QString prefix = dirty ? QStringLiteral("* ") : QString();
    setWindowTitle(QString("%1LaMoshPit \u2014 %2").arg(prefix, name));
}

// =============================================================================
// closeEvent — if there are unsaved changes, prompt Save / Discard / Cancel
// before closing; then persist the current dock layout + window geometry so
// the next launch resumes in the same arrangement.
// =============================================================================
void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_project && m_project->isDirty()) {
        const auto reply = QMessageBox::question(this,
            QStringLiteral("Unsaved Changes"),
            QStringLiteral("The project \"%1\" has unsaved changes.\n\n"
                           "Save before closing?").arg(m_project->name()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (reply == QMessageBox::Cancel) {
            e->ignore();
            return;
        }
        if (reply == QMessageBox::Save) {
            QString err;
            if (!saveActiveProject(err)) {
                QMessageBox::warning(this, QStringLiteral("Save Failed"), err);
                e->ignore();
                return;
            }
        }
        // Discard falls through to close without saving.
    }

    QSettings settings("LaMoshPit", "LaMoshPit");
    KDDockWidgets::LayoutSaver saver;
    settings.setValue("layout/kddwLayout",   saver.serializeLayout());
    settings.setValue("layout/lastGeometry", saveGeometry());
    KDDockWidgets::QtWidgets::MainWindow::closeEvent(e);
}

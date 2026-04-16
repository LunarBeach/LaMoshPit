#pragma once

#include <QVector>
#include <memory>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>
#include "core/model/VideoSequence.h"
#include "gui/BitstreamAnalyzer.h"
#include "core/transform/FrameTransformer.h"
#include "core/model/GlobalEncodeParams.h"

class Project;

class TimelineWidget;
class PreviewPlayer;
class MacroblockWidget;
class GlobalParamsWidget;
class PropertyPanel;
class BitstreamTestWidget;
class QuickMoshWidget;
class MediaBinWidget;
class ProgressPanel;
// The NLE render slot's signature references SequencerRenderer::Params, so
// the full header is pulled in here rather than forward-declared.
#include "core/sequencer/SequencerRenderer.h"

namespace sequencer {
    class SequencerProject;
    class SequencerDock;
}
class QPushButton;
class QProgressBar;
class QLabel;
class QAction;
class QSplitter;
class QDockWidget;
class QMenu;

namespace KDDockWidgets { namespace QtWidgets { class DockWidget; } }

class MainWindow : public KDDockWidgets::QtWidgets::MainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openFile();
    void saveHacked();

    // Timeline selection
    void onSelectionChanged(const QVector<int>& selected);

    // Frame type conversion
    void onForceI();
    void onForceP();
    void onForceB();
    void onDeleteFrames();
    void onDupLeft();
    void onDupRight();
    void onInterpLeft();
    void onInterpRight();
    void onFlip();
    void onFlop();
    // Ctrl+Z handler.  Loads the nearest older version of the current video
    // from its family's .vNN siblings.  See VersionPathUtil::previousVersionPath.
    void onLoadPreviousVersion();
    // MediaBinWidget emits videoSelected(path); MainWindow loads that video.
    void onMediaBinVideoSelected(const QString& videoPath);

    // Project lifecycle — File menu actions.
    void onNewProject();
    void onOpenProject();
    void onSaveProject();

    // File → Import Selection Map.  Opens ImportSelectionMapDialog; the
    // dialog handles probing the map, filtering compatible clips, copying
    // the map into {project}/selection_maps/, and updating the sidecar.
    void onImportSelectionMap();

    // Timeline drag-reorder
    void onFrameReorderRequested(int sourceIdx, int insertBeforeIdx);

    // NLE Sequencer render dialog → background render worker.  The dock
    // emits renderRequested with the user's choices; we own the thread,
    // progress UI, and post-render import-back.  See SequencerDock.h.
    void onNleRenderRequested(const sequencer::SequencerRenderer::Params& params,
                              bool importIntoProject);

    // Worker callbacks
    void onTransformProgress(int current, int total);
    // outputPath: the actual .vNN.mp4 file the render was written to (new
    // versioned sibling).  Empty if no render produced output (failure, no-op
    // reorder, etc.).  MainWindow swaps m_currentVideoPath to this path so
    // the user sees the fresh iteration; the source stays on disk for the bin.
    void onTransformDone(bool success, QString errorMessage, QString outputPath);

    // Timeline ↔ MB editor sync
    void onMBFrameNavigated(int frameIdx);

    // Global params apply
    void onApplyGlobalParams();

    // Quick Mosh user preset signals
    void onQuickMoshSaveUserPreset();
    void onQuickMoshUserMosh(const QString& presetName);

    // Layout menu — save/load named layouts via QSettings + saveState().
    void onSaveLayoutAs();
    void onLoadLayout(const QString& name);
    void onResetLayout();
    void rebuildLayoutMenu();

private:
    void buildLayout();
    void buildMenuBar();
    void applyDefaultLayout();
    KDDockWidgets::QtWidgets::DockWidget* wrapInDock(const QString& title,
                                                      QWidget* widget,
                                                      const QString& uniqueName,
                                                      int dockOptions = 0);
    void analyzeImportedVideo(const QString& videoPath);
    void populateTimeline(const AnalysisReport& report);
    void startTransform(FrameTransformerWorker::TargetType type,
                        const GlobalEncodeParams& globalParams = GlobalEncodeParams(),
                        int interpolateCount = 1);
    void setTransformButtonsEnabled(bool enabled);
    void reloadVideoAndTimeline();
    bool eventFilter(QObject* obj, QEvent* e) override;
    // Persist dock layout + window geometry so the next launch resumes where
    // the user left off. Saved under QSettings key "layout/lastState".
    void closeEvent(QCloseEvent* e) override;

    // Switch the active project.  Tears down current video state, re-points
    // the bin and import path at the new project's folders, and (if the
    // project has an activeVideo set) loads that video automatically.
    // Takes ownership of the passed-in Project.  Called from onNewProject,
    // onOpenProject, and the startup auto-resume path.
    void setActiveProject(std::unique_ptr<Project> p);

    // Resolve the default projects root: Documents/LaMoshPit Projects/.
    // Creates the directory on first call.  Used as the parent for new
    // project folders when the user picks "New Project".
    QString defaultProjectsRoot();

    // ── Layout widgets ────────────────────────────────────────────────────
    TimelineWidget*      m_timeline    { nullptr };
    PreviewPlayer*       m_preview     { nullptr };
    MacroblockWidget*    m_mbWidget    { nullptr };
    GlobalParamsWidget*  m_globalParams{ nullptr };
    PropertyPanel*       m_propertyPanel{ nullptr };
    BitstreamTestWidget* m_bitstreamTest{ nullptr };
    QuickMoshWidget*     m_quickMosh   { nullptr };
    MediaBinWidget*      m_mediaBin    { nullptr };
    ProgressPanel*       m_progressPanel{ nullptr };

    // NLE Sequencer — distinct from the single-clip TimelineWidget in the
    // mosh editor.  Dockable preview + (future) multi-track timeline.
    sequencer::SequencerProject* m_seqProject { nullptr };   // owned by MainWindow
    sequencer::SequencerDock*    m_seqDock    { nullptr };

    // Dock wrappers — one per panel, using KDDockWidgets so floating windows
    // can themselves host nested splits and tabs (native QDockWidget only
    // supports tab-grouping when floating, not side-by-side splits).
    // MB Editor is split into two docks sharing the same MacroblockWidget
    // coordinator — canvas view and controls view are siblings, not nested.
    using KDDW_Dock = KDDockWidgets::QtWidgets::DockWidget;
    KDDW_Dock* m_timelineDock      { nullptr };
    KDDW_Dock* m_previewDock       { nullptr };
    KDDW_Dock* m_mbCanvasDock      { nullptr };
    KDDW_Dock* m_mbEditorDock      { nullptr };
    KDDW_Dock* m_globalParamsDock  { nullptr };
    KDDW_Dock* m_quickMoshDock     { nullptr };
    KDDW_Dock* m_mediaBinDock      { nullptr };
    KDDW_Dock* m_progressDock      { nullptr };
    KDDW_Dock* m_propertyPanelDock { nullptr };
    KDDW_Dock* m_bitstreamTestDock { nullptr };

    // Central widget (timeline strip + frame-action buttons).
    QWidget* m_timelineCentral { nullptr };

    // Layout menu — repopulated whenever QSettings layout list changes.
    QMenu* m_layoutMenu { nullptr };
    QMenu* m_loadLayoutMenu { nullptr };

    // ── Timeline control buttons ──────────────────────────────────────────
    QPushButton*  m_btnForceI    { nullptr };
    QPushButton*  m_btnForceP    { nullptr };
    QPushButton*  m_btnForceB    { nullptr };
    QPushButton*  m_btnDelete    { nullptr };
    QPushButton*  m_btnDupLeft   { nullptr };
    QPushButton*  m_btnDupRight  { nullptr };
    QPushButton*  m_btnInterpLeft { nullptr };
    QPushButton*  m_btnInterpRight{ nullptr };
    QPushButton*  m_btnFlip       { nullptr };
    QPushButton*  m_btnFlop       { nullptr };
    // m_btnUndo removed — undo is now Ctrl+Z → load previous version from the
    // Media Bin's accumulated iterations.  See onLoadPreviousVersion.
    QLabel*       m_selectionLabel{ nullptr };

    // ── View menu actions ─────────────────────────────────────────────────
    // Most panels use their dock's toggleViewAction(). Debug Tools is kept
    // manual because the BitstreamTest widget has no dock-friendly default
    // size (it's a debug-only surface we want hidden until asked for).
    QAction* m_actDebugTools { nullptr };

    // ── State ─────────────────────────────────────────────────────────────
    VideoSequence* m_videoSequence;
    QString        m_currentVideoPath;
    AnalysisReport m_lastAnalysis;

    // Active project.  Exactly one at a time; always non-null after the
    // constructor runs (we auto-create an Untitled project if none is
    // resumable from QSettings).  Owned by MainWindow; lifecycle is scoped
    // to the instance since MainWindow is the only window.
    std::unique_ptr<Project> m_project;

    // m_undoBackupPath / m_hasUndo removed — non-destructive renders mean
    // every iteration is preserved on disk as a new .vNN.mp4 sibling.  "Undo"
    // is now "load a previous version from the bin" (Ctrl+Z or click).  See
    // VersionPathUtil for the filename scheme + previousVersionPath().
    bool    m_transformBusy { false };

    // Tracks which render path was used on the most recent startTransform() —
    // reloadVideoAndTimeline() skips BitstreamAnalyzer re-analysis when this
    // is MBEditOnly (frame types / count unchanged by MB edits, so the cached
    // analysis from before the render is still valid).  Avoids invoking
    // h264bitstream on outputs that contain long runs of P_SKIP MBs — the
    // library is known to crash on such patterns, which was the root cause
    // of the post-render crash observed on bitstream-surgery renders.
    FrameTransformerWorker::TargetType m_lastRenderType {
        FrameTransformerWorker::MBEditOnly };
};

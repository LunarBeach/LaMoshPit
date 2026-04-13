#pragma once

#include <QMainWindow>
#include <QVector>
#include "core/model/VideoSequence.h"
#include "gui/BitstreamAnalyzer.h"
#include "core/transform/FrameTransformer.h"
#include "core/model/GlobalEncodeParams.h"

class TimelineWidget;
class PreviewPlayer;
class MacroblockWidget;
class GlobalParamsWidget;
class PropertyPanel;
class BitstreamTestWidget;
class QuickMoshWidget;
class QPushButton;
class QProgressBar;
class QLabel;
class QAction;
class QSplitter;

class MainWindow : public QMainWindow {
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
    void onUndo();

    // Timeline drag-reorder
    void onFrameReorderRequested(int sourceIdx, int insertBeforeIdx);

    // Worker callbacks
    void onTransformProgress(int current, int total);
    void onTransformDone(bool success, QString errorMessage);

    // Timeline ↔ MB editor sync
    void onMBFrameNavigated(int frameIdx);

    // Global params apply
    void onApplyGlobalParams();

    // Quick Mosh user preset signals
    void onQuickMoshSaveUserPreset();
    void onQuickMoshUserMosh(const QString& presetName);

    // View menu panel toggles
    void togglePanel(QWidget* panel, QAction* action, const QString& name);

private:
    void buildLayout();
    void buildMenuBar();
    void analyzeImportedVideo(const QString& videoPath);
    void populateTimeline(const AnalysisReport& report);
    void startTransform(FrameTransformerWorker::TargetType type,
                        const GlobalEncodeParams& globalParams = GlobalEncodeParams(),
                        int interpolateCount = 1);
    void setTransformButtonsEnabled(bool enabled);
    void reloadVideoAndTimeline();
    bool eventFilter(QObject* obj, QEvent* e) override;

    // ── Layout widgets ────────────────────────────────────────────────────
    TimelineWidget*      m_timeline    { nullptr };
    PreviewPlayer*       m_preview     { nullptr };
    MacroblockWidget*    m_mbWidget    { nullptr };
    GlobalParamsWidget*  m_globalParams{ nullptr };
    PropertyPanel*       m_propertyPanel{ nullptr };
    BitstreamTestWidget* m_bitstreamTest{ nullptr };
    QuickMoshWidget*     m_quickMosh   { nullptr };

    // Outer splitters (kept for size restore on panel show)
    QSplitter* m_topSplitter   { nullptr };
    QSplitter* m_bottomSplitter{ nullptr };
    QSplitter* m_outerSplitter { nullptr };

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
    QPushButton*  m_btnUndo      { nullptr };
    QProgressBar* m_progressBar{ nullptr };
    QLabel*       m_selectionLabel{ nullptr };

    // ── View menu actions ─────────────────────────────────────────────────
    QAction* m_actPreview    { nullptr };
    QAction* m_actMBEditor   { nullptr };
    QAction* m_actQuickMosh  { nullptr };
    QAction* m_actGlobalParams{ nullptr };
    QAction* m_actDebugTools { nullptr };

    // ── Preview pop-out state ─────────────────────────────────────────────
    bool m_previewIsPopped { false };

    // ── State ─────────────────────────────────────────────────────────────
    VideoSequence* m_videoSequence;
    QString        m_currentVideoPath;
    AnalysisReport m_lastAnalysis;

    // Undo: a single backup copy of the imported file
    QString m_undoBackupPath;
    bool    m_hasUndo       { false };
    bool    m_transformBusy { false };
};

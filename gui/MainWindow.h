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
class QPushButton;
class QProgressBar;
class QLabel;

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
    void onApplyMBEdits();
    void onUndo();

    // Worker callbacks
    void onTransformProgress(int current, int total);
    void onTransformDone(bool success, QString errorMessage);

    // Timeline ↔ MB editor sync
    void onMBFrameNavigated(int frameIdx);

    // Global params apply
    void onApplyGlobalParams();

private:
    void analyzeImportedVideo(const QString& videoPath);
    void populateTimeline(const AnalysisReport& report);
    void startTransform(FrameTransformerWorker::TargetType type,
                        const GlobalEncodeParams& globalParams = GlobalEncodeParams());
    void setTransformButtonsEnabled(bool enabled);
    void reloadVideoAndTimeline();

    // ── Layout widgets ────────────────────────────────────────────────────
    TimelineWidget*      m_timeline;
    PreviewPlayer*       m_preview;
    MacroblockWidget*    m_mbWidget;
    GlobalParamsWidget*  m_globalParams;
    PropertyPanel*       m_propertyPanel;
    BitstreamTestWidget* m_bitstreamTest;

    // ── Conversion controls ───────────────────────────────────────────────
    QPushButton*  m_btnForceI;
    QPushButton*  m_btnForceP;
    QPushButton*  m_btnForceB;
    QPushButton*  m_btnDelete;
    QPushButton*  m_btnDupLeft;
    QPushButton*  m_btnDupRight;
    QPushButton*  m_btnApplyMB;
    QPushButton*  m_btnUndo;
    QProgressBar* m_progressBar;
    QLabel*       m_selectionLabel;

    // ── State ─────────────────────────────────────────────────────────────
    VideoSequence* m_videoSequence;
    QString        m_currentVideoPath;
    AnalysisReport m_lastAnalysis;

    // Undo: a single backup copy of the imported file
    QString m_undoBackupPath;
    bool    m_hasUndo = false;
    bool    m_transformBusy = false;
};

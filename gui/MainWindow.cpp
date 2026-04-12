#include "MainWindow.h"
#include "widgets/TimelineWidget.h"
#include "widgets/PreviewPlayer.h"
#include "widgets/MacroblockWidget.h"
#include "widgets/GlobalParamsWidget.h"
#include "widgets/PropertyPanel.h"
#include "widgets/BitstreamTestWidget.h"
#include "widgets/QuickMoshWidget.h"
#include "BitstreamAnalyzer.h"
#include "core/pipeline/DecodePipeline.h"
#include "core/transform/FrameTransformer.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QThread>
#include <QFile>

// =============================================================================
// Constructor / Destructor
// =============================================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_videoSequence(new VideoSequence())
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    // ── Timeline strip ───────────────────────────────────────────────────
    m_timeline = new TimelineWidget(this);
    rootLayout->addWidget(m_timeline);

    connect(m_timeline, &TimelineWidget::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    // ── Conversion controls row ──────────────────────────────────────────
    auto* ctrlRow = new QWidget(this);
    auto* ctrlLayout = new QHBoxLayout(ctrlRow);
    ctrlLayout->setContentsMargins(2, 2, 2, 2);
    ctrlLayout->setSpacing(6);

    m_selectionLabel = new QLabel("No frames selected", this);
    m_selectionLabel->setStyleSheet("color: #888; font: 9pt 'Consolas';");
    ctrlLayout->addWidget(m_selectionLabel);

    ctrlLayout->addStretch(1);

    auto makeBtn = [this](const QString& text, const QString& css) {
        auto* b = new QPushButton(text, this);
        b->setFixedHeight(28);
        b->setMinimumWidth(80);
        b->setEnabled(false);
        b->setStyleSheet(css);
        return b;
    };

    m_btnForceI = makeBtn(
        "Force \u2192 I",
        "QPushButton { background:#222; color:#ffffff; border:2px solid #ffffff; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#555; border-color:#444; }");

    m_btnForceP = makeBtn(
        "Force \u2192 P",
        "QPushButton { background:#222; color:#4488ff; border:2px solid #4488ff; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#336; border-color:#336; }");

    m_btnForceB = makeBtn(
        "Force \u2192 B",
        "QPushButton { background:#222; color:#ff64b4; border:2px solid #ff64b4; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#533; border-color:#533; }");

    m_btnDelete = makeBtn(
        "\u2716 Delete",
        "QPushButton { background:#222; color:#ff4444; border:2px solid #ff4444; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#533; border-color:#422; }");

    m_btnDupLeft = makeBtn(
        "\u276E Dup",
        "QPushButton { background:#222; color:#44ffaa; border:2px solid #44ffaa; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#254; border-color:#253; }");

    m_btnDupRight = makeBtn(
        "Dup \u276F",
        "QPushButton { background:#222; color:#44ffaa; border:2px solid #44ffaa; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#254; border-color:#253; }");

    m_btnApplyMB = makeBtn(
        "Apply MB Edits",
        "QPushButton { background:#222; color:#ffaa00; border:2px solid #ffaa00; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#543; border-color:#432; }");

    m_btnUndo = makeBtn(
        "\u21A9 Undo",
        "QPushButton { background:#222; color:#ffcc44; border:2px solid #ffcc44; "
        "border-radius:4px; font:bold 10pt; }"
        "QPushButton:hover { background:#333; }"
        "QPushButton:disabled { color:#554; border-color:#443; }");
    m_btnUndo->setEnabled(false);

    ctrlLayout->addWidget(m_btnForceI);
    ctrlLayout->addWidget(m_btnForceP);
    ctrlLayout->addWidget(m_btnForceB);
    ctrlLayout->addSpacing(12);
    ctrlLayout->addWidget(m_btnDupLeft);
    ctrlLayout->addWidget(m_btnDupRight);
    ctrlLayout->addSpacing(12);
    ctrlLayout->addWidget(m_btnDelete);
    ctrlLayout->addSpacing(12);
    ctrlLayout->addWidget(m_btnApplyMB);
    ctrlLayout->addSpacing(12);
    ctrlLayout->addWidget(m_btnUndo);
    ctrlLayout->addSpacing(12);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(16);
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background:#1a1a1a; border:1px solid #444; border-radius:3px; }"
        "QProgressBar::chunk { background:#4488ff; }");
    ctrlLayout->addWidget(m_progressBar, 1);

    rootLayout->addWidget(ctrlRow);

    // ── Quick Mosh panel ──────────────────────────────────────────────────
    m_quickMosh = new QuickMoshWidget(this);
    rootLayout->addWidget(m_quickMosh);

    // ── Preview + MB editor + Global Params panel ─────────────────────────
    m_preview      = new PreviewPlayer(this);
    m_mbWidget     = new MacroblockWidget(this);
    connect(m_timeline, &TimelineWidget::selectionChanged,
            m_mbWidget, &MacroblockWidget::setActiveFrameRange);

    m_globalParams = new GlobalParamsWidget(this);
    m_propertyPanel = new PropertyPanel(this);

    auto* midSplitter = new QSplitter(Qt::Horizontal);
    midSplitter->addWidget(m_preview);
    midSplitter->addWidget(m_mbWidget);
    midSplitter->addWidget(m_globalParams);
    midSplitter->addWidget(m_propertyPanel);
    midSplitter->setStretchFactor(0, 3);
    midSplitter->setStretchFactor(1, 2);
    midSplitter->setStretchFactor(2, 2);
    midSplitter->setStretchFactor(3, 0);
    rootLayout->addWidget(midSplitter, 4);

    // ── Bitstream test widget ────────────────────────────────────────────
    m_bitstreamTest = new BitstreamTestWidget(this);
    rootLayout->addWidget(m_bitstreamTest, 1);

    setCentralWidget(central);

    // ── Menu ─────────────────────────────────────────────────────────────
    auto* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Open Video...", this, &MainWindow::openFile);
    fileMenu->addAction("Save Mosh Pit...", this, &MainWindow::saveHacked);

    statusBar()->showMessage("LaMoshPit — Ready for chaos");

    // ── Button connections ────────────────────────────────────────────────
    connect(m_btnForceI,   &QPushButton::clicked, this, &MainWindow::onForceI);
    connect(m_btnForceP,   &QPushButton::clicked, this, &MainWindow::onForceP);
    connect(m_btnForceB,   &QPushButton::clicked, this, &MainWindow::onForceB);
    connect(m_btnDelete,   &QPushButton::clicked, this, &MainWindow::onDeleteFrames);
    connect(m_btnDupLeft,  &QPushButton::clicked, this, &MainWindow::onDupLeft);
    connect(m_btnDupRight, &QPushButton::clicked, this, &MainWindow::onDupRight);
    connect(m_btnApplyMB,  &QPushButton::clicked, this, &MainWindow::onApplyMBEdits);
    connect(m_btnUndo,     &QPushButton::clicked, this, &MainWindow::onUndo);

    // Bidirectional timeline ↔ MB editor sync:
    // MB Prev/Next → update timeline selection to match
    connect(m_mbWidget, &MacroblockWidget::frameNavigated,
            this, &MainWindow::onMBFrameNavigated);

    // MB painter selection → GlobalParamsWidget spatial mask preview
    connect(m_mbWidget, &MacroblockWidget::mbSelectionChanged,
            m_globalParams, &GlobalParamsWidget::updateSpatialMask);

    // GlobalParamsWidget "Apply" button → full re-encode with global params
    connect(m_globalParams, &GlobalParamsWidget::applyRequested,
            this, &MainWindow::onApplyGlobalParams);

    // Quick Mosh panel
    connect(m_quickMosh, &QuickMoshWidget::moshRequested,
            this, &MainWindow::onQuickMosh);
}

MainWindow::~MainWindow()
{
    delete m_videoSequence;
}

// =============================================================================
// File open
// =============================================================================

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Video File", "",
        "Video Files (*.mp4 *.mov *.mkv *.avi *.264 *.h264);;All Files (*.*)");

    if (fileName.isEmpty()) return;

    statusBar()->showMessage("Importing and standardizing video...");

    QDir importDir(QDir::currentPath() + "/imported_videos");
    if (!importDir.exists()) importDir.mkpath(".");

    QString baseName   = QFileInfo(fileName).completeBaseName();
    QString outputPath = importDir.absoluteFilePath(baseName + "_imported.mp4");

    if (!DecodePipeline::standardizeVideo(fileName, outputPath)) {
        statusBar()->showMessage("Import failed.");
        QMessageBox::warning(this, "Import Failed", "Failed to transcode the video.");
        return;
    }

    m_currentVideoPath = outputPath;

    // Clear any undo backup from a previous session
    if (m_hasUndo && !m_undoBackupPath.isEmpty()) {
        QFile::remove(m_undoBackupPath);
        m_hasUndo = false;
    }
    m_btnUndo->setEnabled(false);

    statusBar()->showMessage("Imported: " + baseName + "_imported.mp4 — analyzing...");
    m_preview->loadVideo(m_currentVideoPath);
    m_videoSequence->load(m_currentVideoPath);

    analyzeImportedVideo(m_currentVideoPath);
}

// =============================================================================
// Analysis → timeline population
// =============================================================================

void MainWindow::analyzeImportedVideo(const QString& videoPath)
{
    m_lastAnalysis = BitstreamAnalyzer::analyzeVideo(videoPath);

    if (m_lastAnalysis.success) {
        BitstreamAnalyzer::printAnalysis(m_lastAnalysis);
        BitstreamAnalyzer::saveAnalysisToFile(m_lastAnalysis, videoPath);
        populateTimeline(m_lastAnalysis);
        m_mbWidget->setVideo(videoPath, m_lastAnalysis);
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
}

void MainWindow::populateTimeline(const AnalysisReport& report)
{
    // Use the display-order decoded frames (report.frames) so that the type
    // badge on each thumbnail matches the frame the viewer actually sees.
    // report.slices is in bitstream/decode order which differs for B-frames.
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
    // "Apply MB Edits" and "Mosh Now" are always available once a video is loaded
    m_btnApplyMB->setEnabled(!m_transformBusy);
    m_quickMosh ->setMoshEnabled(!m_transformBusy);
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

        // Sync MB editor to the earliest selected frame.
        // navigateToFrame is a no-op if the MB editor is already there,
        // which prevents a re-trigger loop when the sync goes the other way.
        int earliest = selected.first();
        for (int idx : selected) if (idx < earliest) earliest = idx;
        m_mbWidget->navigateToFrame(earliest);
    }
}

void MainWindow::setTransformButtonsEnabled(bool enabled)
{
    m_btnForceI  ->setEnabled(enabled);
    m_btnForceP  ->setEnabled(enabled);
    m_btnForceB  ->setEnabled(enabled);
    m_btnDelete  ->setEnabled(enabled);
    m_btnDupLeft ->setEnabled(enabled);
    m_btnDupRight->setEnabled(enabled);
    // "Apply MB Edits" and "Mosh Now" don't need a timeline selection —
    // enabled whenever a video is loaded and the transform queue is free.
    m_btnApplyMB ->setEnabled(!m_transformBusy && !m_currentVideoPath.isEmpty());
    m_quickMosh  ->setMoshEnabled(!m_transformBusy && !m_currentVideoPath.isEmpty());
}

// =============================================================================
// Frame type conversion
// =============================================================================

void MainWindow::onForceI()       { startTransform(FrameTransformerWorker::ForceI); }
void MainWindow::onForceP()       { startTransform(FrameTransformerWorker::ForceP); }
void MainWindow::onForceB()       { startTransform(FrameTransformerWorker::ForceB); }
void MainWindow::onDeleteFrames() { startTransform(FrameTransformerWorker::DeleteFrames); }
void MainWindow::onDupLeft()      { startTransform(FrameTransformerWorker::DuplicateLeft); }
void MainWindow::onDupRight()     { startTransform(FrameTransformerWorker::DuplicateRight); }
void MainWindow::onApplyMBEdits() { startTransform(FrameTransformerWorker::MBEditOnly); }

void MainWindow::onApplyGlobalParams()
{
    // Re-encode using the GlobalEncodeParams from the panel.
    // MB edits are included at the same time (additive — both apply together).
    startTransform(FrameTransformerWorker::MBEditOnly, m_globalParams->currentParams());
}

// =============================================================================
// Quick Mosh — build a whole-video MBEditMap from a named preset and fire it
// =============================================================================

void MainWindow::onQuickMosh(int presetIndex)
{
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;

    const int total = m_lastAnalysis.frames.size();
    if (total == 0) return;

    // Find the first P-frame — ideal datamosh seed (never frame 0 which is IDR).
    int seedFrame = 1;
    for (int i = 1; i < (int)m_lastAnalysis.frames.size(); ++i) {
        if (m_lastAnalysis.frames[i].pictType == 'P') { seedFrame = i; break; }
    }

    // Helper: build a cascade seed entry.
    auto makeSeed = [&](int refDepth, int ghostBlend,
                        int mvDriftX, int mvDriftY, int mvAmplify,
                        int noiseLevel, int pixelOffset, int invertLuma,
                        int chromaDriftX, int chromaDriftY, int chromaOffset,
                        int qpDelta, int spillRadius, int sampleRadius,
                        int refScatter, int blockFlatten,
                        int colorTwistU, int colorTwistV,
                        int cascadeLen, int cascadeDecay) -> FrameMBParams {
        FrameMBParams p;
        // selectedMBs intentionally empty — global mode (all MBs) in renderer
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

    // Helper: stamp the same params on every frame (no cascade).
    auto stampAll = [&](MBEditMap& em, const FrameMBParams& tmpl) {
        for (int i = 0; i < total; ++i)
            em[i] = tmpl;
    };

    const int fullCascade = total - seedFrame - 1; // cascade covers rest of video

    MBEditMap edits;

    // Preset index must match QuickMoshWidget kMeta order exactly.
    switch (presetIndex) {
    case 0: // Ghost Smear
        edits[seedFrame] = makeSeed(
            /*rd*/1, /*ghost*/80, /*mvX*/0, /*mvY*/0, /*amp*/1,
            /*noise*/0, /*pxOff*/0, /*inv*/0,
            /*cxX*/0, /*cxY*/0, /*cOff*/0,
            /*qp*/0, /*spill*/0, /*sample*/0, /*scatter*/0, /*flatten*/0,
            /*twU*/0, /*twV*/0,
            /*cascLen*/fullCascade, /*cascDec*/28);
        break;

    case 1: // MV Liquify →
        edits[seedFrame] = makeSeed(
            1, 40, 70, 0, 3,
            0, 0, 0,
            0, 0, 0,
            0, 0, 0, 0, 0,
            0, 0,
            fullCascade, 15);
        break;

    case 2: // MV Liquify ↓
        edits[seedFrame] = makeSeed(
            1, 40, 0, 70, 3,
            0, 0, 0,
            0, 0, 0,
            0, 0, 0, 0, 0,
            0, 0,
            fullCascade, 15);
        break;

    case 3: // Chroma Bleed
        edits[seedFrame] = makeSeed(
            1, 30, 0, 0, 1,
            0, 0, 0,
            45, -25, 20,
            0, 0, 0, 0, 0,
            0, 0,
            fullCascade, 32);
        break;

    case 4: // Vortex
        edits[seedFrame] = makeSeed(
            1, 50, 30, -30, 4,
            60, 20, 0,
            0, 0, 0,
            20, 3, 0, 0, 0,
            0, 0,
            fullCascade, 35);
        break;

    case 5: // Scatter Dissolve
        edits[seedFrame] = makeSeed(
            1, 50, 0, 0, 1,
            0, 0, 0,
            0, 0, 0,
            0, 0, 15, 25, 0,
            0, 0,
            fullCascade, 40);
        break;

    case 6: // Full Freeze
        edits[seedFrame] = makeSeed(
            1, 100, 0, 0, 1,
            0, 0, 0,
            0, 0, 0,
            51, 0, 0, 0, 0,
            0, 0,
            fullCascade, 0);
        break;

    case 7: { // Pixel Disintegrate — stamp every frame
        FrameMBParams tmpl = makeSeed(
            0, 0, 0, 0, 1,
            160, 30, 0,
            0, 0, 0,
            45, 0, 0, 0, 0,
            0, 0,
            0, 0);
        stampAll(edits, tmpl);
        break;
    }

    case 8: { // UV Colour Twist — stamp every frame
        FrameMBParams tmpl = makeSeed(
            1, 30, 0, 0, 1,
            0, 0, 0,
            0, 0, 15,
            0, 0, 0, 0, 0,
            50, -50,
            0, 0);
        stampAll(edits, tmpl);
        break;
    }

    case 9: { // Block Melt — stamp every frame
        FrameMBParams tmpl = makeSeed(
            0, 0, 0, 0, 1,
            80, 0, 0,
            0, 0, 0,
            40, 0, 0, 0, 80,
            0, 0,
            0, 0);
        stampAll(edits, tmpl);
        break;
    }

    default:
        return;
    }

    // Push the edit map into the MB editor so the user can see and adjust it.
    m_mbWidget->loadEditMap(edits);

    // Build encode params — start from whatever the user has configured in the
    // GlobalParams panel, then unconditionally enforce the two settings that
    // datamoshing fundamentally requires regardless of panel state:
    //   killIFrames=true  — without this x264 resets the smear at every I-frame
    //   gopSize=0         — infinite GOP (keyint=9999); prevents periodic keyframes
    // Users can still override these in the panel between mosh passes if wanted.
    GlobalEncodeParams gp = m_globalParams->currentParams();
    gp.killIFrames = true;
    gp.gopSize     = 0;   // → keyint=9999:min-keyint=9999:scenecut=0 in encoder
    startTransform(FrameTransformerWorker::MBEditOnly, gp);
}

void MainWindow::startTransform(FrameTransformerWorker::TargetType type,
                                const GlobalEncodeParams& globalParams)
{
    if (m_transformBusy || m_currentVideoPath.isEmpty()) return;

    QVector<int> sel = m_timeline->selectedFrames();

    if (type == FrameTransformerWorker::MBEditOnly) {
        // MBEditOnly re-encodes the entire file.  It can proceed without any
        // per-MB edits (global params alone still reshape the encoder).
        // Only show a warning when BOTH editMap is empty AND globalParams is
        // entirely default — that combination would just wastefully re-encode.
        bool hasGlobalChange = (globalParams.gopSize != -1 || globalParams.killIFrames ||
            globalParams.bFrames != -1 || globalParams.refFrames != -1 ||
            globalParams.qpOverride != -1 || globalParams.qpMin != -1 || globalParams.qpMax != -1 ||
            globalParams.meMethod != -1 || globalParams.meRange != -1 || globalParams.subpelRef != -1 ||
            globalParams.partitionMode != -1 || globalParams.trellis != -1 ||
            globalParams.noFastPSkip || globalParams.noDctDecimate || globalParams.cabacDisable ||
            globalParams.noDeblock || globalParams.psyRD >= 0.0f || globalParams.aqMode != -1 ||
            globalParams.mbTreeDisable || !globalParams.spatialMaskMBs.isEmpty());
        if (m_mbWidget->editMap().isEmpty() && !hasGlobalChange) {
            statusBar()->showMessage(
                "Nothing to apply — paint MBs or adjust Global Encode Params first.");
            return;
        }
    } else {
        if (sel.isEmpty()) return;
    }

    // Backup current file for one-level undo
    m_undoBackupPath = m_currentVideoPath + ".undo_backup.mp4";
    QFile::remove(m_undoBackupPath);
    if (!QFile::copy(m_currentVideoPath, m_undoBackupPath)) {
        QMessageBox::warning(this, "Backup Failed",
            "Could not create undo backup. Proceeding anyway (no undo available).");
        m_hasUndo   = false;
        m_btnUndo->setEnabled(false);
    } else {
        m_hasUndo = true;
        m_btnUndo->setEnabled(false); // re-enable after transform completes
    }

    // Release the media player's file handle so the worker can replace the file
    m_preview->unloadVideo();

    m_transformBusy = true;
    setTransformButtonsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, m_lastAnalysis.frames.size());
    m_progressBar->setValue(0);

    static const char* names[] = {"I", "P", "B", "deleted", "duplicated left", "duplicated right", "MB edit"};
    if (type == FrameTransformerWorker::MBEditOnly) {
        statusBar()->showMessage(
            QString("Applying MB edits to %1 frame%2 ...")
                .arg(m_lastAnalysis.frames.size())
                .arg(m_lastAnalysis.frames.size() == 1 ? "" : "s"));
    } else {
        statusBar()->showMessage(
            QString("Processing %1 frame%2 → %3 ...")
                .arg(sel.size()).arg(sel.size() == 1 ? "" : "s").arg(names[type]));
    }

    // Build display-order type roster for delete operations so the worker can
    // enforce original frame types on remaining frames (datamosh effect).
    QVector<char> origTypes;
    origTypes.reserve(m_lastAnalysis.frames.size());
    for (const FrameInfo& f : m_lastAnalysis.frames)
        origTypes.append(f.pictType);

    auto* worker = new FrameTransformerWorker(
        m_currentVideoPath, sel, type,
        m_lastAnalysis.frames.size(), origTypes,
        m_mbWidget->editMap(), globalParams, nullptr);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    connect(thread, &QThread::started,
            worker, &FrameTransformerWorker::run);
    connect(worker, &FrameTransformerWorker::progress,
            this,   &MainWindow::onTransformProgress);
    connect(worker, &FrameTransformerWorker::warning,
            this,   [this](const QString& msg){ statusBar()->showMessage("⚠ " + msg); });
    connect(worker, &FrameTransformerWorker::done,
            this,   &MainWindow::onTransformDone);
    connect(worker, &FrameTransformerWorker::done,
            thread, &QThread::quit);
    connect(thread, &QThread::finished,
            worker, &QObject::deleteLater);
    connect(thread, &QThread::finished,
            thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::onTransformProgress(int current, int total)
{
    if (total > 0) m_progressBar->setValue(current);
}

void MainWindow::onTransformDone(bool success, QString errorMessage)
{
    m_transformBusy = false;
    m_progressBar->setVisible(false);

    if (!success) {
        statusBar()->showMessage("Transform failed: " + errorMessage);
        QMessageBox::critical(this, "Transform Failed", errorMessage);
        // Restore from backup if we have one
        if (m_hasUndo) {
            QFile::remove(m_currentVideoPath);
            QFile::copy(m_undoBackupPath, m_currentVideoPath);
        }
        m_hasUndo = false;
        m_btnUndo->setEnabled(false);
        reloadVideoAndTimeline();
        return;
    }

    m_btnUndo->setEnabled(m_hasUndo);
    statusBar()->showMessage("Transform complete — reloading...");
    reloadVideoAndTimeline();
}

// =============================================================================
// Undo
// =============================================================================

void MainWindow::onUndo()
{
    if (!m_hasUndo || m_undoBackupPath.isEmpty()) return;
    if (!QFile::exists(m_undoBackupPath)) {
        QMessageBox::warning(this, "Undo Failed", "Backup file missing.");
        m_hasUndo = false;
        m_btnUndo->setEnabled(false);
        return;
    }

    statusBar()->showMessage("Undoing last transform...");
    QFile::remove(m_currentVideoPath);
    if (QFile::rename(m_undoBackupPath, m_currentVideoPath)) {
        m_hasUndo = false;
        m_btnUndo->setEnabled(false);
        statusBar()->showMessage("Undo complete — reloading...");
        reloadVideoAndTimeline();
    } else {
        QMessageBox::critical(this, "Undo Failed",
            "Could not restore backup. The original may be lost.");
    }
}

// =============================================================================
// Reload after transform/undo
// =============================================================================

void MainWindow::reloadVideoAndTimeline()
{
    m_preview->loadVideo(m_currentVideoPath);
    m_videoSequence->load(m_currentVideoPath);
    // analyzeImportedVideo calls m_mbWidget->setVideo on success (fresh import)
    // but after a transform we call reload so the widget keeps existing edits
    // and just re-decodes the replacement file at the same frame position.
    // We still need the full analysis, so run it first, then call reload.
    m_lastAnalysis = BitstreamAnalyzer::analyzeVideo(m_currentVideoPath);
    if (m_lastAnalysis.success) {
        BitstreamAnalyzer::printAnalysis(m_lastAnalysis);
        BitstreamAnalyzer::saveAnalysisToFile(m_lastAnalysis, m_currentVideoPath);
        populateTimeline(m_lastAnalysis);
        m_mbWidget->reload(m_currentVideoPath, m_lastAnalysis);
        statusBar()->showMessage(
            QString("Ready — %1 frames  I:%2  P:%3  B:%4")
                .arg(m_lastAnalysis.totalFrames)
                .arg(m_lastAnalysis.iFrameCount)
                .arg(m_lastAnalysis.pFrameCount)
                .arg(m_lastAnalysis.bFrameCount));
    } else {
        statusBar()->showMessage("Re-analysis failed: " + m_lastAnalysis.errorMessage);
    }
}

// =============================================================================
// Timeline ↔ MB editor sync
// =============================================================================

void MainWindow::onMBFrameNavigated(int frameIdx)
{
    // User pressed Prev/Next in the MB editor — update timeline selection to
    // reflect the new frame so both views stay in sync.
    m_timeline->setSelection(frameIdx);
}

// =============================================================================
// Save (placeholder)
// =============================================================================

void MainWindow::saveHacked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, "Save Mosh Pit", "", "H.264 MP4 Files (*.mp4)");
    if (!fileName.isEmpty()) {
        // For now: copy the current imported file to the target path
        if (QFile::copy(m_currentVideoPath, fileName))
            statusBar()->showMessage("Saved: " + fileName);
        else
            QMessageBox::warning(this, "Save Failed", "Could not save file.");
    }
}

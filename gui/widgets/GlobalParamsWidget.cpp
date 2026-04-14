#include "GlobalParamsWidget.h"
#include "ToggleSwitch.h"

#include "core/logger/ControlLogger.h"
#include "core/presets/PresetManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFrame>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

// =============================================================================
// Style constants — updated dark theme (white / neon-green text)
// =============================================================================

static const QString kSection =
    "QLabel { color:#00ff88; font:bold 7pt 'Consolas'; "
    "border-bottom:1px solid #1e1e1e; padding-bottom:2px; background:transparent; }";

static const QString kLabel =
    "QLabel { color:#666666; font:7pt 'Consolas'; background:transparent; }";

static const QString kSpin =
    "QSpinBox { background:#111111; color:#cccccc; border:1px solid #282828; "
    "border-radius:3px; font:7pt 'Consolas'; min-width:52px; }"
    "QSpinBox:focus { border-color:#00ff88; }"
    "QSpinBox:disabled { color:#333333; border-color:#1a1a1a; }";

static const QString kDSpin =
    "QDoubleSpinBox { background:#111111; color:#cccccc; border:1px solid #282828; "
    "border-radius:3px; font:7pt 'Consolas'; min-width:52px; }"
    "QDoubleSpinBox:focus { border-color:#00ff88; }"
    "QDoubleSpinBox:disabled { color:#333333; border-color:#1a1a1a; }";

static const QString kCombo =
    "QComboBox { background:#111111; color:#cccccc; border:1px solid #282828; "
    "border-radius:3px; font:7pt 'Consolas'; padding:1px 4px; }"
    "QComboBox:hover { border-color:#3a3a3a; }"
    "QComboBox:focus { border-color:#00ff88; }"
    "QComboBox::drop-down { border:none; width:14px; }"
    "QComboBox QAbstractItemView { background:#111111; color:#cccccc; "
    "selection-background-color:#1a1a1a; selection-color:#00ff88; "
    "border:1px solid #282828; font:7pt 'Consolas'; }";

static const QString kBtn =
    "QPushButton { background:#111111; color:#888888; border:1px solid #282828; "
    "border-radius:3px; font:bold 7pt 'Consolas'; padding:3px 8px; }"
    "QPushButton:hover { background:#181818; color:#ffffff; border-color:#3a3a3a; }"
    "QPushButton:disabled { color:#2a2a2a; border-color:#1a1a1a; }";

static const QString kApplyBtn =
    "QPushButton { background:#0a1a0a; color:#00ff88; border:2px solid #00ff88; "
    "border-radius:5px; font:bold 10pt 'Consolas'; padding:6px 14px; }"
    "QPushButton:hover { background:#112211; border-color:#44ffaa; color:#44ffaa; }"
    "QPushButton:pressed { background:#1a3a1a; }"
    "QPushButton:disabled { color:#1a3a1a; border-color:#112211; }";

// =============================================================================
// Helpers
// =============================================================================

static QLabel* makeSection(const QString& title, QWidget* parent)
{
    auto* lbl = new QLabel(title, parent);
    lbl->setStyleSheet(kSection);
    return lbl;
}

static QLabel* makeLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setStyleSheet(kLabel);
    lbl->setWordWrap(false);
    return lbl;
}

static QSpinBox* addSpin(QGridLayout* g, int row, int col,
                         const QString& lbl, int lo, int hi, int defVal,
                         QWidget* parent)
{
    g->addWidget(makeLabel(lbl, parent), row, col * 2);
    auto* sb = new QSpinBox(parent);
    sb->setRange(lo, hi);
    sb->setValue(defVal);
    sb->setStyleSheet(kSpin);
    sb->setFixedHeight(20);
    g->addWidget(sb, row, col * 2 + 1);
    return sb;
}

static QDoubleSpinBox* addDSpin(QGridLayout* g, int row, int col,
                                const QString& lbl, double lo, double hi,
                                double defVal, double step, QWidget* parent)
{
    g->addWidget(makeLabel(lbl, parent), row, col * 2);
    auto* sb = new QDoubleSpinBox(parent);
    sb->setRange(lo, hi);
    sb->setValue(defVal);
    sb->setSingleStep(step);
    sb->setDecimals(2);
    sb->setStyleSheet(kDSpin);
    sb->setFixedHeight(20);
    g->addWidget(sb, row, col * 2 + 1);
    return sb;
}

static QComboBox* addCombo(QGridLayout* g, int row, int col,
                            const QString& lbl, const QStringList& items,
                            int defIdx, QWidget* parent)
{
    g->addWidget(makeLabel(lbl, parent), row, col * 2);
    auto* cb = new QComboBox(parent);
    cb->addItems(items);
    cb->setCurrentIndex(defIdx);
    cb->setStyleSheet(kCombo);
    cb->setFixedHeight(20);
    g->addWidget(cb, row, col * 2 + 1);
    return cb;
}

// Adds a ToggleSwitch spanning two grid columns (replaces addCheck / QCheckBox).
static ToggleSwitch* addToggle(QGridLayout* g, int row, int col,
                               const QString& lbl, bool def,
                               QWidget* parent,
                               const QColor& onColor = QColor(0x00, 0xff, 0x88))
{
    auto* ts = new ToggleSwitch(lbl, parent);
    ts->setChecked(def);
    ts->setOnColor(onColor);
    g->addWidget(ts, row, col * 2, 1, 2);
    return ts;
}

// =============================================================================
// GlobalParamsWidget constructor
// =============================================================================

GlobalParamsWidget::GlobalParamsWidget(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background:#0a0a0a;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Header ───────────────────────────────────────────────────────────────
    {
        auto* hdr = new QHBoxLayout;
        auto* lbl = new QLabel("GLOBAL ENCODE PARAMS", this);
        lbl->setStyleSheet("color:#ffffff; font:bold 9pt 'Consolas'; background:transparent;");
        hdr->addWidget(lbl);
        hdr->addStretch(1);

        auto* pLbl = new QLabel("Preset:", this);
        pLbl->setStyleSheet("QLabel { color:#555555; font:7pt 'Consolas'; background:transparent; }");
        hdr->addWidget(pLbl);

        m_presetCombo = new QComboBox(this);
        m_presetCombo->addItems({
            "Default (no overrides)",
            "Infinite GOP",
            "Max Datamosh",
            "Smear Heavy",
            "Glitch Wave",
            "Block Mosaic",
            "Chroma Fever",
            "Quantum Residue",
            "Temporal Bleed",
            "Data Corrupt"
        });
        m_presetCombo->setStyleSheet(kCombo);
        m_presetCombo->setFixedHeight(22);
        m_presetCombo->setMinimumWidth(150);
        hdr->addWidget(m_presetCombo);
        root->addLayout(hdr);

        // Preset management buttons
        auto* presetBtnRow = new QHBoxLayout;
        presetBtnRow->setSpacing(4);
        presetBtnRow->addStretch();

        m_btnUserPresetSave   = new QPushButton("Save",   this);
        m_btnUserPresetDel    = new QPushButton("Del",    this);
        m_btnUserPresetImport = new QPushButton("Import", this);

        for (QPushButton* b : {m_btnUserPresetSave, m_btnUserPresetDel, m_btnUserPresetImport})
            b->setStyleSheet(kBtn);

        presetBtnRow->addWidget(m_btnUserPresetSave);
        presetBtnRow->addWidget(m_btnUserPresetDel);
        presetBtnRow->addWidget(m_btnUserPresetImport);
        root->addLayout(presetBtnRow);

        refreshUserPresets();

        connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &GlobalParamsWidget::onPresetSelected);
        connect(m_btnUserPresetSave,   &QPushButton::clicked, this, &GlobalParamsWidget::onUserPresetSave);
        connect(m_btnUserPresetDel,    &QPushButton::clicked, this, &GlobalParamsWidget::onUserPresetDelete);
        connect(m_btnUserPresetImport, &QPushButton::clicked, this, &GlobalParamsWidget::onUserPresetImport);
    }

    // ── Kill I-Frames — prominent top-level toggle ───────────────────────────
    {
        m_cbKillIFrames = new ToggleSwitch(
            "Kill I-Frames  (force all I\u2192P, preserve frame count)", this);
        m_cbKillIFrames->setOnColor(QColor(0xff, 0x55, 0x22)); // orange-red accent
        m_cbKillIFrames->setToolTip(
            "Forces every I-frame (except frame 0) to a P-frame during re-encode.\n"
            "Frame count stays the same. Breaks the GOP structure so the encoder\n"
            "cannot use intra prediction anywhere except the first frame.\n"
            "Essential for classic datamoshing — combine with 'Infinite GOP' preset.");
        root->addWidget(m_cbKillIFrames);
    }

    // ── Scene-cut detection toggle ──────────────────────────────────────────
    {
        m_cbScenecut = new ToggleSwitch(
            "Scene-Cut Detection  (auto I-frames at scene transitions)", this);
        m_cbScenecut->setChecked(false);
        m_cbScenecut->setOnColor(QColor(0x44, 0x88, 0xff)); // blue accent
        m_cbScenecut->setToolTip(
            "When ON, x264 detects scene transitions and automatically inserts\n"
            "I-frames at cuts. OFF by default to prevent surprise I-frames\n"
            "during datamoshing.");
        root->addWidget(m_cbScenecut);
    }

    // ── Control Debug Logging toggle ─────────────────────────────────────────
    {
        m_cbDebugLog = new ToggleSwitch("Enable Control Debug Logging", this);
        m_cbDebugLog->setChecked(false);
        m_cbDebugLog->setOnColor(QColor(0x00, 0xff, 0x88));
        m_cbDebugLog->setToolTip(
            "Logs every knob change, MB paint stroke, and full frame-parameter dump\n"
            "to the VS Code Debug Console AND to:\n"
            "  <exe-dir>/logs/LaMoshPit_ControlTest_Log.txt\n\n"
            "OFF by default (zero performance impact when unchecked).");
        root->addWidget(m_cbDebugLog);

        connect(m_cbDebugLog, &QAbstractButton::toggled,
                this, [](bool on) { ControlLogger::instance().setEnabled(on); });
    }

    // ── Thin divider ─────────────────────────────────────────────────────────
    {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("background:#1e1e1e; border:none;");
        line->setFixedHeight(1);
        root->addWidget(line);
    }

    // ── Scrollable parameter area ────────────────────────────────────────────
    auto* paramContainer = new QWidget(this);
    paramContainer->setStyleSheet("background:#0a0a0a;");
    auto* pv = new QVBoxLayout(paramContainer);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(8);

    auto addSection = [&](const QString& title) -> QGridLayout* {
        pv->addWidget(makeSection(title, paramContainer));
        auto* inner = new QWidget(paramContainer);
        inner->setStyleSheet("background:#0a0a0a;");
        auto* g = new QGridLayout(inner);
        g->setContentsMargins(4, 4, 4, 4);
        g->setHorizontalSpacing(8);
        g->setVerticalSpacing(6);
        g->setColumnStretch(1, 1);
        g->setColumnStretch(3, 1);
        pv->addWidget(inner);
        return g;
    };

    // ── FRAME STRUCTURE ──────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 FRAME STRUCTURE");
        g->addWidget(makeLabel("GOP Size  (-1=dflt, 0=\u221E)", paramContainer), 0, 0);
        m_sbGopSize = new QSpinBox(paramContainer);
        m_sbGopSize->setRange(-1, 9999);
        m_sbGopSize->setValue(-1);
        m_sbGopSize->setToolTip("-1 = encoder default (250)\n0 = infinite GOP\n>0 = exact keyframe interval");
        m_sbGopSize->setStyleSheet(kSpin);
        m_sbGopSize->setFixedHeight(20);
        g->addWidget(m_sbGopSize, 0, 1);

        m_sbBFrames  = addSpin(g, 0, 1, "B-Frames (-1=dflt)", -1, 16, -1, paramContainer);
        m_cbBAdapt   = addCombo(g, 1, 0, "B-Adapt",
            {"Default","Off","Fast","Trellis"}, 0, paramContainer);
        m_sbRefFrames = addSpin(g, 1, 1, "Ref Frames (-1=dflt)", -1, 16, -1, paramContainer);
        m_sbRefFrames->setToolTip("-1=default (3). More refs = wider temporal reach = more smear.");
    }

    // ── RATE CONTROL ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 RATE CONTROL");
        m_sbQPOverride = addSpin(g, 0, 0, "QP Override (-1=CRF18)", -1, 51, -1, paramContainer);
        m_sbQPMin      = addSpin(g, 0, 1, "QP Min (-1=dflt)",       -1, 51, -1, paramContainer);
        m_sbQPMax      = addSpin(g, 1, 0, "QP Max (-1=dflt)",       -1, 51, -1, paramContainer);
        m_sbQPMax->setToolTip("51 = worst quantiser = max corruption");
    }

    // ── MOTION ESTIMATION ────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 MOTION ESTIMATION");
        m_cbMEMethod = addCombo(g, 0, 0, "ME Method",
            {"Default","dia (inaccurate)","hex","umh","esa (full)","tesa"}, 0, paramContainer);
        m_sbMERange   = addSpin(g, 0, 1, "ME Range (0=dflt)", 0, 512, 0, paramContainer);
        m_sbSubpelRef = addSpin(g, 1, 0, "Subpel Ref (0..10, -1=dflt)", -1, 10, -1, paramContainer);
    }

    // ── PARTITIONS & DCT ─────────────────────────────────────────────────────
    //
    // Three dropdowns — one per H.264 slice type — controlling which partition
    // subdivisions x264 is allowed to emit.  Each dropdown maps to a subset of
    // x264's --partitions flag bits:
    //
    //   I-frame MB Type → i4x4, i8x8  (intra partitions; note these also govern
    //                     the minority of intra MBs that appear in P/B frames)
    //   P-frame MB Type → p8x8, p4x4  (P-slice inter partitions; p4x4 implies p8x8)
    //   B-frame MB Type → b8x8        (B-slice bi-directional inter partitions;
    //                     H.264 has no b4x4 so only two options for B)
    //
    // Dropdown index → GlobalEncodeParams value (index − 1):
    //   0 Default      → -1  (use x264's natural default for this frame type)
    //   1 16×16 only   →  0  (no subdivision for this frame type)
    //   2 +8×8         →  1  (one level of subdivision)
    //   3 +8×8 +4×4    →  2  (full subdivision — I and P only)
    //
    // NOTE: Force Skip (MB Editor, per-MB) ALWAYS takes precedence on flagged
    // MBs since skip MBs are 16×16 by spec and bypass partition analysis.
    {
        auto* g = addSection("\u2500\u2500 MB TYPE (PER FRAME TYPE) & DCT");
        m_cbIFrameMbType = addCombo(g, 0, 0, "I-frame MB Type",
            {"Default", "16\u00D716 only", "+8\u00D78", "+8\u00D78 +4\u00D74"},
            0, paramContainer);
        m_cbPFrameMbType = addCombo(g, 0, 1, "P-frame MB Type",
            {"Default", "16\u00D716 only", "+8\u00D78", "+8\u00D78 +4\u00D74"},
            0, paramContainer);
        m_cbBFrameMbType = addCombo(g, 1, 0, "B-frame MB Type",
            {"Default", "16\u00D716 only", "+8\u00D78"},
            0, paramContainer);
        m_cbx8x8DCT      = addToggle(g, 1, 1, "8\u00D78 DCT", true, paramContainer);
    }

    // ── B-FRAME PREDICTION ───────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 B-FRAME PREDICTION");
        m_cbDirectMode = addCombo(g, 0, 0, "Direct Mode",
            {"Default","None","Temporal","Spatial","Auto"}, 0, paramContainer);
        m_cbWeightedB  = addToggle(g, 0, 1, "Weighted-B Pred", true, paramContainer);
        m_cbWeightedP  = addCombo(g, 1, 0, "Weighted-P Pred",
            {"Default","Off","Blind","Smart"}, 0, paramContainer);
    }

    // ── QUANTIZATION FLAGS ───────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 QUANTIZATION FLAGS");
        m_cbTrellis       = addCombo(g, 0, 0, "Trellis",
            {"Default","Off","Final MB only","All MBs"}, 0, paramContainer);
        m_cbNoFastPSkip   = addToggle(g, 1, 0, "No Fast P-Skip",   false, paramContainer);
        m_cbNoDctDecimate = addToggle(g, 1, 1, "No DCT Decimate",  false, paramContainer);
        m_cbCabacDisable  = addToggle(g, 2, 0, "CAVLC (no CABAC)", false, paramContainer);
    }

    // ── DEBLOCKING ───────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 DEBLOCKING FILTER");
        m_cbNoDeblock = addToggle(g, 0, 0, "Disable Loop Filter", false, paramContainer,
                                  QColor(0xff, 0x88, 0x00));  // orange = destructive
        m_sbDeblockA  = addSpin(g, 0, 1, "Alpha \u03B1", -6, 6, 0, paramContainer);
        m_sbDeblockB  = addSpin(g, 1, 0, "Beta \u03B2",  -6, 6, 0, paramContainer);
    }

    // ── PSYCHOVISUAL ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 PSYCHOVISUAL OPTIMISATION");
        m_dsbPsyRD       = addDSpin(g, 0, 0, "Psy-RD (-1=dflt)",    -1.0, 5.0, -1.0, 0.1, paramContainer);
        m_dsbPsyTrellis  = addDSpin(g, 0, 1, "Psy-Trellis",         -1.0, 1.0, -1.0, 0.05, paramContainer);
        m_cbAQMode       = addCombo(g, 1, 0, "AQ Mode",
            {"Default","Off","Variance","Auto-Var","Auto+Edge"}, 0, paramContainer);
        m_dsbAQStrength  = addDSpin(g, 1, 1, "AQ Strength",         -1.0, 3.0, -1.0, 0.1, paramContainer);
        m_cbMBTreeDisable = addToggle(g, 2, 0, "Disable MB-Tree RC", false, paramContainer);
        m_sbLookahead    = addSpin(g, 2, 1, "Lookahead (-1=dflt)",  -1, 250, -1, paramContainer);
    }

    // ── RATE-CONTROL FIDELITY ────────────────────────────────────────────────
    // These parameters inhibit x264's ability to override per-MB user edits.
    // Each control is gated by a toggle — OFF = x264 uses its own default.
    {
        auto* g = addSection("\u2500\u2500 RATE-CONTROL FIDELITY");

        // Helper: create a toggle + control pair where the control is disabled until toggled on.
        auto addGatedDSpin = [&](QGridLayout* grid, int row, int col,
                                  const QString& lbl, double lo, double hi,
                                  double defVal, double step,
                                  ToggleSwitch*& toggleOut, QDoubleSpinBox*& sbOut) {
            toggleOut = addToggle(grid, row, col, lbl, false, paramContainer);
            sbOut = addDSpin(grid, row, col == 0 ? 1 : col, "", lo, hi, defVal, step, paramContainer);
            // Replace the empty label that addDSpin created
            sbOut->setEnabled(false);
            connect(toggleOut, &QAbstractButton::toggled, sbOut, &QWidget::setEnabled);
        };

        auto addGatedSpin = [&](QGridLayout* grid, int row, int col,
                                 const QString& lbl, int lo, int hi, int defVal,
                                 ToggleSwitch*& toggleOut, QSpinBox*& sbOut) {
            toggleOut = addToggle(grid, row, col, lbl, false, paramContainer);
            sbOut = addSpin(grid, row, col == 0 ? 1 : col, "", lo, hi, defVal, paramContainer);
            sbOut->setEnabled(false);
            connect(toggleOut, &QAbstractButton::toggled, sbOut, &QWidget::setEnabled);
        };

        addGatedDSpin(g, 0, 0, "QP Comp",        0.0, 1.0, 0.6, 0.05, m_cbQcompEnable,    m_dsbQcomp);
        addGatedDSpin(g, 0, 1, "I/P Ratio",      1.0, 2.0, 1.4, 0.1,  m_cbIpratioEnable,  m_dsbIpratio);
        addGatedDSpin(g, 1, 0, "P/B Ratio",      1.0, 2.0, 1.3, 0.1,  m_cbPbratioEnable,  m_dsbPbratio);
        addGatedSpin (g, 1, 1, "DZ Inter",        0,  32,  21,          m_cbDzInterEnable,  m_sbDzInter);
        addGatedSpin (g, 2, 0, "DZ Intra",        0,  32,  11,          m_cbDzIntraEnable,  m_sbDzIntra);
        addGatedDSpin(g, 2, 1, "QP Blur",        0.0, 10.0, 0.5, 0.1,  m_cbQblurEnable,    m_dsbQblur);
    }

    // ── SPATIAL MASK ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 SPATIAL MASK (from MB Painter)");

        m_maskLabel = new QLabel("No mask captured", paramContainer);
        m_maskLabel->setStyleSheet(
            "QLabel { color:#3a3a3a; font:7pt 'Consolas'; font-style:italic; background:transparent; }");
        g->addWidget(m_maskLabel, 0, 0, 1, 4);

        m_btnCaptureMask = new QPushButton("Capture Current MB Selection", paramContainer);
        m_btnCaptureMask->setStyleSheet(kBtn);
        m_btnCaptureMask->setFixedHeight(22);
        g->addWidget(m_btnCaptureMask, 1, 0, 1, 2);

        auto* clrBtn = new QPushButton("Clear Mask", paramContainer);
        clrBtn->setStyleSheet(kBtn);
        clrBtn->setFixedHeight(22);
        g->addWidget(clrBtn, 1, 2, 1, 2);

        g->addWidget(makeLabel("Mask QP (1..51)", paramContainer), 2, 0);
        m_sbMaskQP = new QSpinBox(paramContainer);
        m_sbMaskQP->setRange(1, 51);
        m_sbMaskQP->setValue(51);
        m_sbMaskQP->setStyleSheet(kSpin);
        m_sbMaskQP->setFixedHeight(20);
        g->addWidget(m_sbMaskQP, 2, 1);

        connect(m_btnCaptureMask, &QPushButton::clicked, this, [this]() {
            m_maskLabel->setText(
                QString("%1 MBs captured as spatial mask").arg(m_spatialMask.size()));
            m_maskLabel->setStyleSheet(
                "QLabel { color:#00ff88; font:7pt 'Consolas'; background:transparent; }");
        });
        connect(clrBtn, &QPushButton::clicked, this, [this]() {
            m_spatialMask.clear();
            m_maskLabel->setText("No mask captured");
            m_maskLabel->setStyleSheet(
                "QLabel { color:#3a3a3a; font:7pt 'Consolas'; font-style:italic; background:transparent; }");
        });
    }

    pv->addStretch(1);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(paramContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background:#0a0a0a; border:none; }"
        "QScrollBar:vertical { background:#0e0e0e; width:7px; border:none; }"
        "QScrollBar::handle:vertical { background:#2a2a2a; border-radius:3px; min-height:20px; }"
        "QScrollBar::handle:vertical:hover { background:#00ff88; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    root->addWidget(scrollArea, 1);

    // ── Apply button ─────────────────────────────────────────────────────────
    m_btnApply = new QPushButton("RENDER", this);
    m_btnApply->setStyleSheet(kApplyBtn);
    m_btnApply->setFixedHeight(34);
    m_btnApply->setToolTip(
        "Re-encodes the video applying all Global Params and any MB edits currently\n"
        "in the editor. Select the Default preset to encode with no overrides.\n"
        "Changes compound: each Render reads the previously-encoded file.");
    root->addWidget(m_btnApply);

    connect(m_btnApply, &QPushButton::clicked,
            this, &GlobalParamsWidget::applyRequested);
}

// =============================================================================
// Preset loading
// =============================================================================

static constexpr int kGPBuiltinCount = 10;

void GlobalParamsWidget::onPresetSelected(int idx)
{
    static const EncodePreset presets[] = {
        EncodePreset::Default,
        EncodePreset::InfiniteGOP,
        EncodePreset::MaxDatamosh,
        EncodePreset::SmearHeavy,
        EncodePreset::GlitchWave,
        EncodePreset::BlockMosaic,
        EncodePreset::ChromaFever,
        EncodePreset::QuantumResidue,
        EncodePreset::TemporalBleed,
        EncodePreset::DataCorrupt,
    };
    if (idx < 0) return;

    if (idx < kGPBuiltinCount) {
        // Built-in preset
        setParams(presetParams(presets[idx]));
    } else if (idx > kGPBuiltinCount) {
        // User preset (kGPBuiltinCount is the separator)
        const QString name = m_presetCombo->itemText(idx);
        GlobalEncodeParams p;
        if (PresetManager::loadGlobalEncode(name, p))
            setParams(p);
    }

    // Del only works on user presets
    if (m_btnUserPresetDel)
        m_btnUserPresetDel->setEnabled(idx > kGPBuiltinCount);
}

// =============================================================================
// currentParams
// =============================================================================

GlobalEncodeParams GlobalParamsWidget::currentParams() const
{
    GlobalEncodeParams p;

    p.killIFrames = m_cbKillIFrames->isChecked();
    p.scenecut    = m_cbScenecut->isChecked();
    p.gopSize    = m_sbGopSize->value();
    p.bFrames    = m_sbBFrames->value();
    p.bAdapt     = m_cbBAdapt->currentIndex() - 1;
    p.refFrames  = m_sbRefFrames->value();

    p.qpOverride = m_sbQPOverride->value();
    p.qpMin      = m_sbQPMin->value();
    p.qpMax      = m_sbQPMax->value();

    p.meMethod   = m_cbMEMethod->currentIndex() - 1;
    p.meRange    = m_sbMERange->value();
    p.subpelRef  = m_sbSubpelRef->value();

    p.iFrameMbType = m_cbIFrameMbType->currentIndex() - 1;
    p.pFrameMbType = m_cbPFrameMbType->currentIndex() - 1;
    p.bFrameMbType = m_cbBFrameMbType->currentIndex() - 1;
    p.use8x8DCT    = m_cbx8x8DCT->isChecked();

    p.directMode    = m_cbDirectMode->currentIndex() - 1;
    p.weightedPredB = m_cbWeightedB->isChecked();
    p.weightedPredP = m_cbWeightedP->currentIndex() - 1;

    p.trellis        = m_cbTrellis->currentIndex() - 1;
    p.noFastPSkip    = m_cbNoFastPSkip->isChecked();
    p.noDctDecimate  = m_cbNoDctDecimate->isChecked();
    p.cabacDisable   = m_cbCabacDisable->isChecked();

    p.noDeblock    = m_cbNoDeblock->isChecked();
    p.deblockAlpha = m_sbDeblockA->value();
    p.deblockBeta  = m_sbDeblockB->value();

    p.psyRD       = (float)m_dsbPsyRD->value();
    p.psyTrellis  = (float)m_dsbPsyTrellis->value();
    p.aqMode      = m_cbAQMode->currentIndex() - 1;
    p.aqStrength  = (float)m_dsbAQStrength->value();
    p.mbTreeDisable = m_cbMBTreeDisable->isChecked();
    p.rcLookahead   = m_sbLookahead->value();

    // Rate-control fidelity (gated by enable toggles)
    p.qcompEnabled         = m_cbQcompEnable->isChecked();
    p.qcomp                = (float)m_dsbQcomp->value();
    p.ipratioEnabled       = m_cbIpratioEnable->isChecked();
    p.ipratio              = (float)m_dsbIpratio->value();
    p.pbratioEnabled       = m_cbPbratioEnable->isChecked();
    p.pbratio              = (float)m_dsbPbratio->value();
    p.deadzoneInterEnabled = m_cbDzInterEnable->isChecked();
    p.deadzoneInter        = m_sbDzInter->value();
    p.deadzoneIntraEnabled = m_cbDzIntraEnable->isChecked();
    p.deadzoneIntra        = m_sbDzIntra->value();
    p.qblurEnabled         = m_cbQblurEnable->isChecked();
    p.qblur                = (float)m_dsbQblur->value();

    p.spatialMaskMBs = m_spatialMask;
    p.spatialMaskQP  = m_sbMaskQP->value();

    return p;
}

// =============================================================================
// setParams
// =============================================================================

void GlobalParamsWidget::setParams(const GlobalEncodeParams& p)
{
    m_cbKillIFrames->setChecked(p.killIFrames);
    m_cbScenecut->setChecked(p.scenecut);
    m_sbGopSize->setValue(p.gopSize);
    m_sbBFrames->setValue(p.bFrames);
    m_cbBAdapt->setCurrentIndex(qBound(0, p.bAdapt + 1, m_cbBAdapt->count() - 1));
    m_sbRefFrames->setValue(p.refFrames);

    m_sbQPOverride->setValue(p.qpOverride);
    m_sbQPMin->setValue(p.qpMin);
    m_sbQPMax->setValue(p.qpMax);

    m_cbMEMethod->setCurrentIndex(qBound(0, p.meMethod + 1, m_cbMEMethod->count() - 1));
    m_sbMERange->setValue(p.meRange);
    m_sbSubpelRef->setValue(p.subpelRef);

    m_cbIFrameMbType->setCurrentIndex(
        qBound(0, p.iFrameMbType + 1, m_cbIFrameMbType->count() - 1));
    m_cbPFrameMbType->setCurrentIndex(
        qBound(0, p.pFrameMbType + 1, m_cbPFrameMbType->count() - 1));
    m_cbBFrameMbType->setCurrentIndex(
        qBound(0, p.bFrameMbType + 1, m_cbBFrameMbType->count() - 1));
    m_cbx8x8DCT->setChecked(p.use8x8DCT);

    m_cbDirectMode->setCurrentIndex(qBound(0, p.directMode + 1, m_cbDirectMode->count() - 1));
    m_cbWeightedB->setChecked(p.weightedPredB);
    m_cbWeightedP->setCurrentIndex(qBound(0, p.weightedPredP + 1, m_cbWeightedP->count() - 1));

    m_cbTrellis->setCurrentIndex(qBound(0, p.trellis + 1, m_cbTrellis->count() - 1));
    m_cbNoFastPSkip->setChecked(p.noFastPSkip);
    m_cbNoDctDecimate->setChecked(p.noDctDecimate);
    m_cbCabacDisable->setChecked(p.cabacDisable);

    m_cbNoDeblock->setChecked(p.noDeblock);
    m_sbDeblockA->setValue(p.deblockAlpha);
    m_sbDeblockB->setValue(p.deblockBeta);

    m_dsbPsyRD->setValue(p.psyRD);
    m_dsbPsyTrellis->setValue(p.psyTrellis);
    m_cbAQMode->setCurrentIndex(qBound(0, p.aqMode + 1, m_cbAQMode->count() - 1));
    m_dsbAQStrength->setValue(p.aqStrength);
    m_cbMBTreeDisable->setChecked(p.mbTreeDisable);
    m_sbLookahead->setValue(p.rcLookahead);

    // Rate-control fidelity
    m_cbQcompEnable->setChecked(p.qcompEnabled);
    m_dsbQcomp->setValue(p.qcomp);
    m_dsbQcomp->setEnabled(p.qcompEnabled);
    m_cbIpratioEnable->setChecked(p.ipratioEnabled);
    m_dsbIpratio->setValue(p.ipratio);
    m_dsbIpratio->setEnabled(p.ipratioEnabled);
    m_cbPbratioEnable->setChecked(p.pbratioEnabled);
    m_dsbPbratio->setValue(p.pbratio);
    m_dsbPbratio->setEnabled(p.pbratioEnabled);
    m_cbDzInterEnable->setChecked(p.deadzoneInterEnabled);
    m_sbDzInter->setValue(p.deadzoneInter);
    m_sbDzInter->setEnabled(p.deadzoneInterEnabled);
    m_cbDzIntraEnable->setChecked(p.deadzoneIntraEnabled);
    m_sbDzIntra->setValue(p.deadzoneIntra);
    m_sbDzIntra->setEnabled(p.deadzoneIntraEnabled);
    m_cbQblurEnable->setChecked(p.qblurEnabled);
    m_dsbQblur->setValue(p.qblur);
    m_dsbQblur->setEnabled(p.qblurEnabled);
}

// =============================================================================
// Spatial mask
// =============================================================================

void GlobalParamsWidget::updateSpatialMask(const QSet<int>& mbs)
{
    m_spatialMask = mbs;
    if (!mbs.isEmpty())
        m_maskLabel->setText(
            QString("%1 MBs ready to capture").arg(mbs.size()));
}

// =============================================================================
// User preset management
// =============================================================================

void GlobalParamsWidget::refreshUserPresets()
{
    if (!m_presetCombo) return;
    QSignalBlocker sb(m_presetCombo);

    // Remove everything after built-in items (separator + old user items)
    while (m_presetCombo->count() > kGPBuiltinCount)
        m_presetCombo->removeItem(m_presetCombo->count() - 1);

    const QStringList names = PresetManager::list(PresetManager::Type::GlobalEncode);
    if (!names.isEmpty()) {
        m_presetCombo->insertSeparator(kGPBuiltinCount);
        for (const QString& n : names)
            m_presetCombo->addItem(n);
    }

    if (m_btnUserPresetDel)
        m_btnUserPresetDel->setEnabled(!names.isEmpty() &&
            m_presetCombo->currentIndex() > kGPBuiltinCount);
}

void GlobalParamsWidget::onUserPresetSave()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Save Global Encode Preset",
        "Preset name:",
        QLineEdit::Normal,
        QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!PresetManager::saveGlobalEncode(name, currentParams())) {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not save preset \"%1\".").arg(name));
        return;
    }
    refreshUserPresets();
    const int idx = m_presetCombo->findText(PresetManager::sanitize(name));
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

void GlobalParamsWidget::onUserPresetDelete()
{
    const int ci = m_presetCombo->currentIndex();
    if (ci <= kGPBuiltinCount) return; // can't delete built-in or separator

    const QString name = m_presetCombo->currentText();
    if (name.isEmpty()) return;

    const auto btn = QMessageBox::question(this, "Delete Preset",
        QString("Delete preset \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    PresetManager::deletePreset(PresetManager::Type::GlobalEncode, name);
    refreshUserPresets();
}

void GlobalParamsWidget::onUserPresetImport()
{
    const QString src = QFileDialog::getOpenFileName(this,
        "Import Global Encode Preset", QString(),
        "JSON Preset Files (*.json);;All Files (*)");
    if (src.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        "Import Preset",
        "Name for imported preset:",
        QLineEdit::Normal,
        QFileInfo(src).completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!PresetManager::importFile(PresetManager::Type::GlobalEncode, src, name)) {
        QMessageBox::warning(this, "Import Failed",
            "The selected file does not appear to be a Global Encode preset.");
        return;
    }
    refreshUserPresets();
    const int idx = m_presetCombo->findText(PresetManager::sanitize(name));
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

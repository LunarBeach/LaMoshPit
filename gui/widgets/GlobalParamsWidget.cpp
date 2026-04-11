#include "GlobalParamsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFrame>

// =============================================================================
// Style constants
// =============================================================================

static const QString kSection =
    "QLabel { color:#666; font:bold 7pt 'Consolas'; "
    "border-bottom:1px solid #2a2a2a; padding-bottom:2px; }";

static const QString kLabel =
    "QLabel { color:#888; font:7pt 'Consolas'; }";

static const QString kSpin =
    "QSpinBox { background:#1a1a1a; color:#ccc; border:1px solid #444; "
    "font:7pt 'Consolas'; min-width:52px; }"
    "QSpinBox:disabled { color:#444; }";

static const QString kDSpin =
    "QDoubleSpinBox { background:#1a1a1a; color:#ccc; border:1px solid #444; "
    "font:7pt 'Consolas'; min-width:52px; }"
    "QDoubleSpinBox:disabled { color:#444; }";

static const QString kCombo =
    "QComboBox { background:#1a1a1a; color:#ccc; border:1px solid #444; "
    "font:7pt 'Consolas'; }"
    "QComboBox QAbstractItemView { background:#1a1a1a; color:#ccc; "
    "selection-background-color:#333; }";

static const QString kCheck =
    "QCheckBox { color:#aaa; font:7pt 'Consolas'; }"
    "QCheckBox::indicator { width:12px; height:12px; background:#1a1a1a; "
    "border:1px solid #555; border-radius:2px; }"
    "QCheckBox::indicator:checked { background:#4488ff; border-color:#4488ff; }";

static const QString kBtn =
    "QPushButton { background:#222; color:#ccc; border:1px solid #555; "
    "border-radius:3px; font:bold 8pt 'Consolas'; padding:4px 10px; }"
    "QPushButton:hover { background:#2e2e2e; }"
    "QPushButton:disabled { color:#444; border-color:#333; }";

static const QString kApplyBtn =
    "QPushButton { background:#222; color:#ffaa00; border:2px solid #ffaa00; "
    "border-radius:4px; font:bold 9pt 'Consolas'; padding:5px 12px; }"
    "QPushButton:hover { background:#2e2e2e; }"
    "QPushButton:disabled { color:#543; border-color:#432; }";

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

// Adds a labelled spin box in two columns of a QGridLayout.
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
                                double defVal, double step,
                                QWidget* parent)
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
                            const QString& lbl,
                            const QStringList& items,
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

static QCheckBox* addCheck(QGridLayout* g, int row, int col,
                            const QString& lbl, bool def, QWidget* parent)
{
    auto* cb = new QCheckBox(lbl, parent);
    cb->setChecked(def);
    cb->setStyleSheet(kCheck);
    g->addWidget(cb, row, col * 2, 1, 2); // span two cols
    return cb;
}

// =============================================================================
// GlobalParamsWidget constructor
// =============================================================================

GlobalParamsWidget::GlobalParamsWidget(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background:#111;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(4);

    // ── Header: preset selector ─────────────────────────────────────────────
    {
        auto* hdr = new QHBoxLayout;
        auto* lbl = new QLabel("GLOBAL ENCODE PARAMS", this);
        lbl->setStyleSheet("color:#4488ff; font:bold 8pt 'Consolas';");
        hdr->addWidget(lbl);
        hdr->addStretch(1);

        auto* pLbl = new QLabel("Preset:", this);
        pLbl->setStyleSheet(kLabel);
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
        m_presetCombo->setMinimumWidth(140);
        hdr->addWidget(m_presetCombo);

        root->addLayout(hdr);

        connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &GlobalParamsWidget::onPresetSelected);
    }

    // ── Kill I-Frames checkbox (top-level, prominent) ────────────────────────
    {
        m_cbKillIFrames = new QCheckBox("Kill I-Frames  (force all I→P, preserve frame count)", this);
        m_cbKillIFrames->setStyleSheet(
            "QCheckBox { color:#ff6644; font:bold 8pt 'Consolas'; }"
            "QCheckBox::indicator { width:14px; height:14px; background:#1a1a1a; "
            "border:1px solid #ff6644; border-radius:2px; }"
            "QCheckBox::indicator:checked { background:#ff6644; }");
        m_cbKillIFrames->setToolTip(
            "Forces every I-frame (except frame 0) to a P-frame during re-encode.\n"
            "Frame count stays the same. Breaks the GOP structure so the encoder\n"
            "cannot use intra prediction anywhere except the first frame.\n"
            "Essential for classic datamoshing — combine with 'Infinite GOP' preset.");
        root->addWidget(m_cbKillIFrames);
    }

    // ── Scrollable parameter area ────────────────────────────────────────────
    auto* paramContainer = new QWidget(this);
    paramContainer->setStyleSheet("background:#111;");
    auto* pv = new QVBoxLayout(paramContainer);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(6);

    // Helper: build a grid inside a section
    auto addSection = [&](const QString& title) -> QGridLayout* {
        pv->addWidget(makeSection(title, paramContainer));
        auto* inner = new QWidget(paramContainer);
        auto* g = new QGridLayout(inner);
        g->setContentsMargins(2, 2, 2, 2);
        g->setHorizontalSpacing(6);
        g->setVerticalSpacing(4);
        g->setColumnStretch(1, 1);
        g->setColumnStretch(3, 1);
        pv->addWidget(inner);
        return g;
    };

    // ── FRAME STRUCTURE ──────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 FRAME STRUCTURE");
        // gopSize: 0=default(250), 1=infinite(9999), >1=exact
        // We expose it as: -1=encoder default, 0=infinite, >0=exact
        // Spinbox range -1..9999, -1=default
        g->addWidget(makeLabel("GOP Size  (-1=dflt, 0=\u221E)", paramContainer), 0, 0);
        m_sbGopSize = new QSpinBox(paramContainer);
        m_sbGopSize->setRange(-1, 9999);
        m_sbGopSize->setValue(-1);
        m_sbGopSize->setToolTip("-1 = encoder default (250)\n0 = infinite GOP (only explicit I-frames)\n>0 = exact keyframe interval");
        m_sbGopSize->setStyleSheet(kSpin);
        m_sbGopSize->setFixedHeight(20);
        g->addWidget(m_sbGopSize, 0, 1);

        m_sbBFrames = addSpin(g, 0, 1, "B-Frames (-1=dflt)", -1, 16, -1, paramContainer);
        m_sbBFrames->setToolTip("-1 = encoder default (3)");

        m_cbBAdapt = addCombo(g, 1, 0, "B-Adapt",
            {"Default", "Off", "Fast", "Trellis"}, 0, paramContainer);
        m_cbBAdapt->setToolTip("How the encoder decides where to place B-frames");

        m_sbRefFrames = addSpin(g, 1, 1, "Ref Frames (-1=dflt)", -1, 16, -1, paramContainer);
        m_sbRefFrames->setToolTip("-1=default (3). More refs = wider temporal reach = more smear.");
    }

    // ── RATE CONTROL ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 RATE CONTROL");
        m_sbQPOverride = addSpin(g, 0, 0, "QP Override (-1=CRF18)", -1, 51, -1, paramContainer);
        m_sbQPOverride->setToolTip("-1=use CRF 18. 0..51=force fixed QP (51=all artefacts)");
        m_sbQPMin      = addSpin(g, 0, 1, "QP Min (-1=dflt)", -1, 51, -1, paramContainer);
        m_sbQPMax      = addSpin(g, 1, 0, "QP Max (-1=dflt)", -1, 51, -1, paramContainer);
        m_sbQPMax->setToolTip("51 = worst quantiser, zero residual rounding = max corruption");
    }

    // ── MOTION ESTIMATION ────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 MOTION ESTIMATION");
        m_cbMEMethod = addCombo(g, 0, 0, "ME Method",
            {"Default", "dia (inaccurate)", "hex", "umh", "esa (full)", "tesa"},
            0, paramContainer);
        m_cbMEMethod->setToolTip("dia = fastest/least accurate = most wrong MVs = most artefact\n"
                                 "esa/tesa = most accurate = fewest artefacts");
        m_sbMERange = addSpin(g, 0, 1, "ME Range (0=dflt)", 0, 512, 0, paramContainer);
        m_sbMERange->setToolTip("Pixel search window. Narrow=wrong MVs=smear; wide=accurate");
        m_sbSubpelRef = addSpin(g, 1, 0, "Subpel Ref (0..10, -1=dflt)", -1, 10, -1, paramContainer);
        m_sbSubpelRef->setToolTip("0=integer pixel MVs only (step-like artefacts)\n10=full subpel precision");
    }

    // ── PARTITIONS & DCT ─────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 PARTITIONS & DCT");
        m_cbPartition = addCombo(g, 0, 0, "Partition Mode",
            {"Default", "16\u00D716 only", "p8\u00D78", "All", "All+4\u00D74"},
            0, paramContainer);
        m_cbPartition->setToolTip("16x16 only = crude big-block prediction = grid artefacts\n"
                                   "All+4x4 = very fine, accurate = few artefacts");
        m_cbx8x8DCT = addCheck(g, 0, 1, "8\u00D78 DCT", true, paramContainer);
        m_cbx8x8DCT->setToolTip("Uncheck for 4x4 DCT only (lower spatial frequency capture)");
    }

    // ── B-FRAME PREDICTION ───────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 B-FRAME PREDICTION");
        m_cbDirectMode = addCombo(g, 0, 0, "Direct Mode",
            {"Default", "None", "Temporal", "Spatial", "Auto"}, 0, paramContainer);
        m_cbDirectMode->setToolTip("How B-frame direct/skip MVs are derived from neighbours");
        m_cbWeightedB  = addCheck(g, 0, 1, "Weighted-B Pred", true, paramContainer);
        m_cbWeightedP  = addCombo(g, 1, 0, "Weighted-P Pred",
            {"Default", "Off", "Blind", "Smart"}, 0, paramContainer);
    }

    // ── QUANTIZATION FLAGS ───────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 QUANTIZATION FLAGS");
        m_cbTrellis = addCombo(g, 0, 0, "Trellis",
            {"Default", "Off", "Final MB only", "All MBs"}, 0, paramContainer);
        m_cbTrellis->setToolTip("Off = no RD-optimal coefficient zeroing = noisier residuals");
        m_cbNoFastPSkip  = addCheck(g, 1, 0, "No Fast P-Skip", false, paramContainer);
        m_cbNoFastPSkip->setToolTip("Force full residual evaluation on every MB (no bail-out)");
        m_cbNoDctDecimate = addCheck(g, 1, 1, "No DCT Decimate", false, paramContainer);
        m_cbNoDctDecimate->setToolTip("Preserve near-zero DCT coefficients instead of zeroing");
        m_cbCabacDisable  = addCheck(g, 2, 0, "CAVLC (no CABAC)", false, paramContainer);
        m_cbCabacDisable->setToolTip("Less efficient entropy coding = different bitstream artefacts");
    }

    // ── DEBLOCKING ───────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 DEBLOCKING FILTER");
        m_cbNoDeblock = addCheck(g, 0, 0, "Disable Loop Filter", false, paramContainer);
        m_cbNoDeblock->setToolTip("Preserves raw 16x16 block boundaries (grid artefact)");
        m_sbDeblockA  = addSpin(g, 0, 1, "Alpha \u03B1", -6, 6, 0, paramContainer);
        m_sbDeblockB  = addSpin(g, 1, 0, "Beta \u03B2", -6, 6, 0, paramContainer);
    }

    // ── PSYCHOVISUAL ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 PSYCHOVISUAL OPTIMISATION");
        m_dsbPsyRD = addDSpin(g, 0, 0, "Psy-RD (-1=dflt)", -1.0, 5.0, -1.0, 0.1, paramContainer);
        m_dsbPsyRD->setToolTip("0=off (flat distribution). >1=adds grain to textures");
        m_dsbPsyTrellis = addDSpin(g, 0, 1, "Psy-Trellis", -1.0, 1.0, -1.0, 0.05, paramContainer);
        m_cbAQMode = addCombo(g, 1, 0, "AQ Mode",
            {"Default", "Off", "Variance", "Auto-Var", "Auto+Edge"},
            0, paramContainer);
        m_cbAQMode->setToolTip("Adaptive quantisation mode. Off=flat QP across frame");
        m_dsbAQStrength = addDSpin(g, 1, 1, "AQ Strength", -1.0, 3.0, -1.0, 0.1, paramContainer);
        m_cbMBTreeDisable = addCheck(g, 2, 0, "Disable MB-Tree RC", false, paramContainer);
        m_cbMBTreeDisable->setToolTip("Disables macroblock-tree lookahead rate control");
        m_sbLookahead = addSpin(g, 2, 1, "Lookahead (-1=dflt)", -1, 250, -1, paramContainer);
    }

    // ── SPATIAL MASK ─────────────────────────────────────────────────────────
    {
        auto* g = addSection("\u2500\u2500 SPATIAL MASK (from MB Painter)");
        m_maskLabel = new QLabel("No mask captured", paramContainer);
        m_maskLabel->setStyleSheet("color:#666; font:7pt 'Consolas'; font-style:italic;");
        g->addWidget(m_maskLabel, 0, 0, 1, 4);

        m_btnCaptureMask = new QPushButton("Capture Current MB Selection", paramContainer);
        m_btnCaptureMask->setStyleSheet(kBtn);
        m_btnCaptureMask->setFixedHeight(22);
        m_btnCaptureMask->setToolTip(
            "Copies the current MB painter selection into the spatial mask.\n"
            "On every encoded frame, the mask QP is applied to those MBs\n"
            "as a persistent per-region quantiser override.");
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
        m_sbMaskQP->setToolTip("QP applied to masked MBs on every frame.\n"
                               "51=max quantisation (forces zero residuals = skip blocks)");
        g->addWidget(m_sbMaskQP, 2, 1);

        connect(m_btnCaptureMask, &QPushButton::clicked, this, [this]() {
            // mask already stored in m_spatialMask via updateSpatialMask()
            // Button text confirms it was captured
            m_maskLabel->setText(
                QString("%1 MBs captured as spatial mask").arg(m_spatialMask.size()));
            m_maskLabel->setStyleSheet("color:#44ff88; font:7pt 'Consolas';");
        });
        connect(clrBtn, &QPushButton::clicked, this, [this]() {
            m_spatialMask.clear();
            m_maskLabel->setText("No mask captured");
            m_maskLabel->setStyleSheet("color:#666; font:7pt 'Consolas'; font-style:italic;");
        });
    }

    pv->addStretch(1);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(paramContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background:#111; border:none; }"
        "QScrollBar:vertical { background:#1a1a1a; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#444; border-radius:4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    root->addWidget(scrollArea, 1);

    // ── Apply button ─────────────────────────────────────────────────────────
    m_btnApply = new QPushButton("Apply Global Params + Re-encode", this);
    m_btnApply->setStyleSheet(kApplyBtn);
    m_btnApply->setFixedHeight(30);
    m_btnApply->setToolTip(
        "Triggers a full re-encode of the entire video using these settings.\n"
        "Changes compound: each apply reads the previously-damaged file.");
    root->addWidget(m_btnApply);

    connect(m_btnApply, &QPushButton::clicked,
            this, &GlobalParamsWidget::applyRequested);
}

// =============================================================================
// Preset loading
// =============================================================================

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
    if (idx < 0 || idx >= (int)(sizeof(presets)/sizeof(presets[0]))) return;
    setParams(presetParams(presets[idx]));
}

// =============================================================================
// currentParams
// =============================================================================

GlobalEncodeParams GlobalParamsWidget::currentParams() const
{
    GlobalEncodeParams p;

    p.killIFrames = m_cbKillIFrames->isChecked();
    p.gopSize    = m_sbGopSize->value();
    p.bFrames    = m_sbBFrames->value();
    p.bAdapt     = m_cbBAdapt->currentIndex() - 1; // 0→-1(dflt), 1→0, 2→1, 3→2
    p.refFrames  = m_sbRefFrames->value();

    p.qpOverride = m_sbQPOverride->value();
    p.qpMin      = m_sbQPMin->value();
    p.qpMax      = m_sbQPMax->value();

    // ME method: 0=default → -1; 1=dia→0; 2=hex→1; …
    p.meMethod   = m_cbMEMethod->currentIndex() - 1;
    p.meRange    = m_sbMERange->value();
    p.subpelRef  = m_sbSubpelRef->value();

    p.partitionMode = m_cbPartition->currentIndex() - 1; // 0→-1(dflt), 1→0…
    p.use8x8DCT     = m_cbx8x8DCT->isChecked();

    // direct mode: 0=default→-1; 1=none→0; 2=temporal→1; …
    p.directMode    = m_cbDirectMode->currentIndex() - 1;
    p.weightedPredB = m_cbWeightedB->isChecked();
    p.weightedPredP = m_cbWeightedP->currentIndex() - 1; // 0→-1, 1→0, …

    p.trellis        = m_cbTrellis->currentIndex() - 1;  // 0→-1, 1→0, …
    p.noFastPSkip    = m_cbNoFastPSkip->isChecked();
    p.noDctDecimate  = m_cbNoDctDecimate->isChecked();
    p.cabacDisable   = m_cbCabacDisable->isChecked();

    p.noDeblock    = m_cbNoDeblock->isChecked();
    p.deblockAlpha = m_sbDeblockA->value();
    p.deblockBeta  = m_sbDeblockB->value();

    p.psyRD       = (float)m_dsbPsyRD->value();
    p.psyTrellis  = (float)m_dsbPsyTrellis->value();
    p.aqMode      = m_cbAQMode->currentIndex() - 1;  // 0→-1, 1→0 …
    p.aqStrength  = (float)m_dsbAQStrength->value();
    p.mbTreeDisable = m_cbMBTreeDisable->isChecked();
    p.rcLookahead   = m_sbLookahead->value();

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

    m_cbPartition->setCurrentIndex(qBound(0, p.partitionMode + 1, m_cbPartition->count() - 1));
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
}

// =============================================================================
// Spatial mask
// =============================================================================

void GlobalParamsWidget::updateSpatialMask(const QSet<int>& mbs)
{
    m_spatialMask = mbs;
    // Show a live count so the user knows the selection was received.
    if (!mbs.isEmpty())
        m_maskLabel->setText(
            QString("%1 MBs ready to capture").arg(mbs.size()));
}

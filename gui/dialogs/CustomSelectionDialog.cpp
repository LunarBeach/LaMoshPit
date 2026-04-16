#include "CustomSelectionDialog.h"

#include <QTimer>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QCheckBox>
#include <QRandomGenerator>
#include <algorithm>

// =============================================================================
// Direction names (3-way knob labels)
// =============================================================================
static const char* kRowDirNames[3]     = { "Right", "Left", "Outward" };
static const char* kColDirNames[3]     = { "Down",  "Up",   "Outward" };
static const char* kRowSeedDirNames[3] = { "Down",  "Up",   "Outward" };
static const char* kColSeedDirNames[3] = { "Right", "Left", "Outward" };

static const char* kGrpSS =
    "QGroupBox { color:#ccc; font:bold 8pt 'Consolas'; "
    "border:1px solid #333; border-radius:4px; margin-top:10px; padding:8px; }"
    "QGroupBox::title { subcontrol-origin: margin; left:8px; padding:0 4px; }";

// =============================================================================
// Helper: make a labeled horizontal slider with value readout.
// Returns {slider, valueLabel}.  Auto-connects slider→label to display value.
// =============================================================================
struct LabelledSlider {
    QSlider* slider;
    QLabel*  valueLabel;
};

static LabelledSlider makeSlider(QWidget* parent,
                                 int min, int max, int init,
                                 const QString& suffix = QString(),
                                 int width = 180)
{
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    s->setValue(qBound(min, init, max));
    s->setFixedWidth(width);
    s->setStyleSheet(
        "QSlider::groove:horizontal { background:#1e1e1e; height:5px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:#888; width:12px; height:12px; "
        "  margin:-4px 0; border-radius:6px; }"
        "QSlider::sub-page:horizontal { background:#555; border-radius:2px; }");
    auto* l = new QLabel(QString::number(s->value()) + suffix, parent);
    l->setFixedWidth(62);
    l->setStyleSheet("color:#aaa; font:8pt 'Consolas';");
    QObject::connect(s, &QSlider::valueChanged, l, [l, suffix](int v) {
        l->setText(QString::number(v) + suffix);
    });
    return { s, l };
}

// Build a 3-way direction dial with a caption label.
static QDial* makeDirDial(QWidget* parent, QLabel*& caption,
                          const char* initialText)
{
    auto* d = new QDial(parent);
    d->setRange(0, 2);
    d->setNotchesVisible(true);
    d->setNotchTarget(3);
    d->setWrapping(false);
    d->setFixedSize(48, 48);
    d->setValue(0);
    caption = new QLabel(initialText, parent);
    caption->setFixedWidth(70);
    caption->setStyleSheet("color:#00ff88; font:bold 8pt 'Consolas';");
    return d;
}

// =============================================================================
// Construction
// =============================================================================
CustomSelectionDialog::CustomSelectionDialog(int mbCols, int mbRows,
                                             const QSet<int>& existingSelection,
                                             QWidget* parent)
    : QDialog(parent),
      m_mbCols(qMax(1, mbCols)),
      m_mbRows(qMax(1, mbRows)),
      m_existing(existingSelection)
{
    setWindowTitle("Custom Selection");
    setModal(true);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }"
                  "QCheckBox { color:#ccc; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Frame grid: %1 × %2 macroblocks\n"
                "Enabled sections union into the current painted selection.")
            .arg(m_mbCols).arg(m_mbRows), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    root->addWidget(buildRandomGroup());
    root->addWidget(buildRowsGroup());
    root->addWidget(buildColsGroup());

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshRowRanges();
    refreshColRanges();

    // ── Wire live-preview broadcasting ───────────────────────────────────────
    // Every control that can alter the computed selection pushes a preview
    // event back through emitPreview(). The caller pipes this into the MB
    // canvas so the grid highlights update as the user drags.
    auto wire = [this](auto* w, auto sig) {
        connect(w, sig, this, &CustomSelectionDialog::emitPreview);
    };
    wire(m_randEnable,  &QCheckBox::toggled);
    wire(m_randPct,     &QSlider::valueChanged);

    wire(m_rowEnable,   &QCheckBox::toggled);
    wire(m_rowStartX,   &QSlider::valueChanged);
    wire(m_rowStartY,   &QSlider::valueChanged);
    wire(m_rowLength,   &QSlider::valueChanged);
    wire(m_rowThickness,&QSlider::valueChanged);
    wire(m_rowDir,      &QDial::valueChanged);
    wire(m_rowSeedDups, &QSlider::valueChanged);
    wire(m_rowSeedGap,  &QSlider::valueChanged);
    wire(m_rowSeedDir,  &QDial::valueChanged);

    wire(m_colEnable,   &QCheckBox::toggled);
    wire(m_colStartX,   &QSlider::valueChanged);
    wire(m_colStartY,   &QSlider::valueChanged);
    wire(m_colLength,   &QSlider::valueChanged);
    wire(m_colThickness,&QSlider::valueChanged);
    wire(m_colDir,      &QDial::valueChanged);
    wire(m_colSeedDups, &QSlider::valueChanged);
    wire(m_colSeedGap,  &QSlider::valueChanged);
    wire(m_colSeedDir,  &QDial::valueChanged);

    // Initial preview so the canvas reflects the default state as soon as
    // the dialog opens (even before the user touches a control).
    QTimer::singleShot(0, this, &CustomSelectionDialog::emitPreview);
}

void CustomSelectionDialog::emitPreview()
{
    QSet<int> merged = m_existing;
    merged |= computedSelection();
    emit selectionPreview(merged);
}

// =============================================================================
// Section: RANDOM
// =============================================================================
QGroupBox* CustomSelectionDialog::buildRandomGroup()
{
    auto* box = new QGroupBox("Random Selection", this);
    box->setStyleSheet(kGrpSS);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(10, 14, 10, 10);
    lay->setSpacing(6);

    m_randEnable = new QCheckBox("Enable random MB selection", box);
    lay->addWidget(m_randEnable);

    auto pct = makeSlider(box, 0, 100, 10, "%", 260);
    m_randPct    = pct.slider;
    m_randPctLbl = pct.valueLabel;

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    auto* lbl = new QLabel("Percentage:", box);
    lbl->setFixedWidth(80);
    row->addWidget(lbl);
    row->addWidget(m_randPct);
    row->addWidget(m_randPctLbl);
    row->addStretch(1);
    lay->addLayout(row);
    return box;
}

// =============================================================================
// Section: ROWS
// =============================================================================
QGroupBox* CustomSelectionDialog::buildRowsGroup()
{
    auto* box = new QGroupBox("Row Selection", this);
    box->setStyleSheet(kGrpSS);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(10, 14, 10, 10);
    lay->setSpacing(6);

    m_rowEnable = new QCheckBox("Enable row selection", box);
    lay->addWidget(m_rowEnable);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    int r = 0;

    auto addSliderRow = [&](const char* name, QSlider* s, QLabel* valLbl) {
        auto* l = new QLabel(name, box);
        l->setFixedWidth(90);
        grid->addWidget(l,      r, 0);
        grid->addWidget(s,      r, 1);
        grid->addWidget(valLbl, r, 2);
        ++r;
    };

    auto startX = makeSlider(box, 0, m_mbCols - 1, m_mbCols / 2);
    m_rowStartX = startX.slider;  m_rowStartXLbl = startX.valueLabel;
    addSliderRow("Start X:", m_rowStartX, m_rowStartXLbl);

    auto startY = makeSlider(box, 0, m_mbRows - 1, m_mbRows / 2);
    m_rowStartY = startY.slider;  m_rowStartYLbl = startY.valueLabel;
    addSliderRow("Start Y:", m_rowStartY, m_rowStartYLbl);

    auto len = makeSlider(box, 1, m_mbCols, qMax(1, m_mbCols / 2));
    m_rowLength = len.slider;     m_rowLengthLbl = len.valueLabel;
    addSliderRow("Row length:", m_rowLength, m_rowLengthLbl);

    // Direction dial
    {
        auto* dirRow = new QHBoxLayout();
        dirRow->setSpacing(8);
        auto* l = new QLabel("Row direction:", box);
        l->setFixedWidth(90);
        m_rowDir = makeDirDial(box, m_rowDirLbl, kRowDirNames[0]);
        dirRow->addWidget(l);
        dirRow->addWidget(m_rowDir);
        dirRow->addWidget(m_rowDirLbl);
        dirRow->addStretch(1);
        grid->addLayout(dirRow, r++, 0, 1, 3);
    }

    auto thick = makeSlider(box, 1, m_mbRows, 1);
    m_rowThickness = thick.slider;  m_rowThicknessLbl = thick.valueLabel;
    addSliderRow("Thickness:", m_rowThickness, m_rowThicknessLbl);

    lay->addLayout(grid);

    // Row Seed sub-box
    auto* seedBox = new QGroupBox("Row Seed (duplicate bands)", box);
    seedBox->setStyleSheet(kGrpSS);
    auto* seedLay = new QGridLayout(seedBox);
    seedLay->setHorizontalSpacing(8);
    seedLay->setVerticalSpacing(4);
    seedLay->setContentsMargins(10, 14, 10, 10);
    int sr = 0;
    auto addSeedRow = [&](const char* name, QSlider* s, QLabel* v) {
        auto* l = new QLabel(name, seedBox);
        l->setFixedWidth(90);
        seedLay->addWidget(l, sr, 0);
        seedLay->addWidget(s, sr, 1);
        seedLay->addWidget(v, sr, 2);
        ++sr;
    };

    auto sDups = makeSlider(seedBox, 0, qMax(0, m_mbRows - 1), 0);
    m_rowSeedDups = sDups.slider;  m_rowSeedDupsLbl = sDups.valueLabel;
    addSeedRow("Duplicates:", m_rowSeedDups, m_rowSeedDupsLbl);

    auto sGap = makeSlider(seedBox, 0, qMax(0, m_mbRows - 1), 1);
    m_rowSeedGap = sGap.slider;    m_rowSeedGapLbl = sGap.valueLabel;
    addSeedRow("Vertical gap:", m_rowSeedGap, m_rowSeedGapLbl);

    {
        auto* dirRow = new QHBoxLayout();
        dirRow->setSpacing(8);
        auto* l = new QLabel("Seed direction:", seedBox);
        l->setFixedWidth(90);
        m_rowSeedDir = makeDirDial(seedBox, m_rowSeedDirLbl, kRowSeedDirNames[0]);
        dirRow->addWidget(l);
        dirRow->addWidget(m_rowSeedDir);
        dirRow->addWidget(m_rowSeedDirLbl);
        dirRow->addStretch(1);
        seedLay->addLayout(dirRow, sr++, 0, 1, 3);
    }

    lay->addWidget(seedBox);

    // Wire dynamic-range updates
    auto refresh = [this]() { refreshRowRanges(); };
    connect(m_rowStartX,   &QSlider::valueChanged, this, refresh);
    connect(m_rowStartY,   &QSlider::valueChanged, this, refresh);
    connect(m_rowLength,   &QSlider::valueChanged, this, refresh);
    connect(m_rowThickness,&QSlider::valueChanged, this, refresh);
    connect(m_rowSeedGap,  &QSlider::valueChanged, this, refresh);
    connect(m_rowDir,      &QDial::valueChanged,   this, [this](int v) {
        m_rowDirLbl->setText(kRowDirNames[qBound(0, v, 2)]);
        refreshRowRanges();
    });
    connect(m_rowSeedDir,  &QDial::valueChanged,   this, [this](int v) {
        m_rowSeedDirLbl->setText(kRowSeedDirNames[qBound(0, v, 2)]);
        refreshRowRanges();
    });
    return box;
}

// =============================================================================
// Section: COLUMNS  (mirror of rows with X/Y swapped)
// =============================================================================
QGroupBox* CustomSelectionDialog::buildColsGroup()
{
    auto* box = new QGroupBox("Column Selection", this);
    box->setStyleSheet(kGrpSS);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(10, 14, 10, 10);
    lay->setSpacing(6);

    m_colEnable = new QCheckBox("Enable column selection", box);
    lay->addWidget(m_colEnable);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    int r = 0;
    auto addSliderRow = [&](const char* name, QSlider* s, QLabel* v) {
        auto* l = new QLabel(name, box);
        l->setFixedWidth(90);
        grid->addWidget(l, r, 0);
        grid->addWidget(s, r, 1);
        grid->addWidget(v, r, 2);
        ++r;
    };

    auto startX = makeSlider(box, 0, m_mbCols - 1, m_mbCols / 2);
    m_colStartX = startX.slider;  m_colStartXLbl = startX.valueLabel;
    addSliderRow("Start X:", m_colStartX, m_colStartXLbl);

    auto startY = makeSlider(box, 0, m_mbRows - 1, m_mbRows / 2);
    m_colStartY = startY.slider;  m_colStartYLbl = startY.valueLabel;
    addSliderRow("Start Y:", m_colStartY, m_colStartYLbl);

    auto len = makeSlider(box, 1, m_mbRows, qMax(1, m_mbRows / 2));
    m_colLength = len.slider;     m_colLengthLbl = len.valueLabel;
    addSliderRow("Column length:", m_colLength, m_colLengthLbl);

    {
        auto* dirRow = new QHBoxLayout();
        dirRow->setSpacing(8);
        auto* l = new QLabel("Column direction:", box);
        l->setFixedWidth(90);
        m_colDir = makeDirDial(box, m_colDirLbl, kColDirNames[0]);
        dirRow->addWidget(l);
        dirRow->addWidget(m_colDir);
        dirRow->addWidget(m_colDirLbl);
        dirRow->addStretch(1);
        grid->addLayout(dirRow, r++, 0, 1, 3);
    }

    auto thick = makeSlider(box, 1, m_mbCols, 1);
    m_colThickness = thick.slider;  m_colThicknessLbl = thick.valueLabel;
    addSliderRow("Thickness:", m_colThickness, m_colThicknessLbl);

    lay->addLayout(grid);

    auto* seedBox = new QGroupBox("Column Seed (duplicate bands)", box);
    seedBox->setStyleSheet(kGrpSS);
    auto* seedLay = new QGridLayout(seedBox);
    seedLay->setHorizontalSpacing(8);
    seedLay->setVerticalSpacing(4);
    seedLay->setContentsMargins(10, 14, 10, 10);
    int sr = 0;
    auto addSeedRow = [&](const char* name, QSlider* s, QLabel* v) {
        auto* l = new QLabel(name, seedBox);
        l->setFixedWidth(90);
        seedLay->addWidget(l, sr, 0);
        seedLay->addWidget(s, sr, 1);
        seedLay->addWidget(v, sr, 2);
        ++sr;
    };

    auto sDups = makeSlider(seedBox, 0, qMax(0, m_mbCols - 1), 0);
    m_colSeedDups = sDups.slider;  m_colSeedDupsLbl = sDups.valueLabel;
    addSeedRow("Duplicates:", m_colSeedDups, m_colSeedDupsLbl);

    auto sGap = makeSlider(seedBox, 0, qMax(0, m_mbCols - 1), 1);
    m_colSeedGap = sGap.slider;    m_colSeedGapLbl = sGap.valueLabel;
    addSeedRow("Horizontal gap:", m_colSeedGap, m_colSeedGapLbl);

    {
        auto* dirRow = new QHBoxLayout();
        dirRow->setSpacing(8);
        auto* l = new QLabel("Seed direction:", seedBox);
        l->setFixedWidth(90);
        m_colSeedDir = makeDirDial(seedBox, m_colSeedDirLbl, kColSeedDirNames[0]);
        dirRow->addWidget(l);
        dirRow->addWidget(m_colSeedDir);
        dirRow->addWidget(m_colSeedDirLbl);
        dirRow->addStretch(1);
        seedLay->addLayout(dirRow, sr++, 0, 1, 3);
    }

    lay->addWidget(seedBox);

    auto refresh = [this]() { refreshColRanges(); };
    connect(m_colStartX,   &QSlider::valueChanged, this, refresh);
    connect(m_colStartY,   &QSlider::valueChanged, this, refresh);
    connect(m_colLength,   &QSlider::valueChanged, this, refresh);
    connect(m_colThickness,&QSlider::valueChanged, this, refresh);
    connect(m_colSeedGap,  &QSlider::valueChanged, this, refresh);
    connect(m_colDir,      &QDial::valueChanged,   this, [this](int v) {
        m_colDirLbl->setText(kColDirNames[qBound(0, v, 2)]);
        refreshColRanges();
    });
    connect(m_colSeedDir,  &QDial::valueChanged,   this, [this](int v) {
        m_colSeedDirLbl->setText(kColSeedDirNames[qBound(0, v, 2)]);
        refreshColRanges();
    });
    return box;
}

// =============================================================================
// Dynamic range refresh
// =============================================================================
// Row length ceiling based on startX + direction.
//   Right:   mbCols - startX
//   Left:    startX + 1
//   Out:     2 * min(startX, mbCols-1-startX) + 1
static int maxBandLength(int centre, int dir, int limit) {
    switch (dir) {
    case 0: return qMax(1, limit - centre);
    case 1: return qMax(1, centre + 1);
    case 2: return qMax(1, 2 * qMin(centre, limit - 1 - centre) + 1);
    }
    return 1;
}

// Given centre, thickness, limit, seed direction, seed gap:
// Returns how many duplicate bands fit before any band's centre leaves [0,limit-1].
static int maxSeedDups(int centre, int thickness, int gap,
                       int seedDir, int limit)
{
    const int step = qMax(1, thickness + gap);
    const int upRoom   = centre;                 // centres at centre-step, centre-2*step, ...
    const int downRoom = (limit - 1) - centre;
    switch (seedDir) {
    case 0: return qMax(0, downRoom / step);              // Down / Right
    case 1: return qMax(0, upRoom   / step);              // Up   / Left
    case 2: return qMax(0, (upRoom / step) + (downRoom / step)); // alternating both
    }
    return 0;
}

void CustomSelectionDialog::refreshRowRanges() {
    // 1) Row length depends on startX & direction.
    const int lenMax = maxBandLength(m_rowStartX->value(),
                                     m_rowDir->value(), m_mbCols);
    m_rowLength->blockSignals(true);
    m_rowLength->setMaximum(lenMax);
    if (m_rowLength->value() > lenMax) m_rowLength->setValue(lenMax);
    m_rowLength->blockSignals(false);

    // 2) Thickness max = mbRows (hard cap; alternating growth clips at edges).
    m_rowThickness->blockSignals(true);
    m_rowThickness->setMaximum(m_mbRows);
    if (m_rowThickness->value() > m_mbRows) m_rowThickness->setValue(m_mbRows);
    m_rowThickness->blockSignals(false);

    // 3) Seed-gap range: 0..mbRows-1.  Already fixed at construction.

    // 4) Duplicate count depends on thickness + gap + startY + seedDir.
    const int dupsMax = maxSeedDups(m_rowStartY->value(),
                                    m_rowThickness->value(),
                                    m_rowSeedGap->value(),
                                    m_rowSeedDir->value(),
                                    m_mbRows);
    m_rowSeedDups->blockSignals(true);
    m_rowSeedDups->setMaximum(dupsMax);
    if (m_rowSeedDups->value() > dupsMax) m_rowSeedDups->setValue(dupsMax);
    m_rowSeedDups->blockSignals(false);
    // Force label refresh in case setValue was a no-op.
    m_rowSeedDupsLbl->setText(QString::number(m_rowSeedDups->value()));
    m_rowLengthLbl  ->setText(QString::number(m_rowLength->value()));
    m_rowThicknessLbl->setText(QString::number(m_rowThickness->value()));
}

void CustomSelectionDialog::refreshColRanges() {
    const int lenMax = maxBandLength(m_colStartY->value(),
                                     m_colDir->value(), m_mbRows);
    m_colLength->blockSignals(true);
    m_colLength->setMaximum(lenMax);
    if (m_colLength->value() > lenMax) m_colLength->setValue(lenMax);
    m_colLength->blockSignals(false);

    m_colThickness->blockSignals(true);
    m_colThickness->setMaximum(m_mbCols);
    if (m_colThickness->value() > m_mbCols) m_colThickness->setValue(m_mbCols);
    m_colThickness->blockSignals(false);

    const int dupsMax = maxSeedDups(m_colStartX->value(),
                                    m_colThickness->value(),
                                    m_colSeedGap->value(),
                                    m_colSeedDir->value(),
                                    m_mbCols);
    m_colSeedDups->blockSignals(true);
    m_colSeedDups->setMaximum(dupsMax);
    if (m_colSeedDups->value() > dupsMax) m_colSeedDups->setValue(dupsMax);
    m_colSeedDups->blockSignals(false);

    m_colSeedDupsLbl ->setText(QString::number(m_colSeedDups->value()));
    m_colLengthLbl   ->setText(QString::number(m_colLength->value()));
    m_colThicknessLbl->setText(QString::number(m_colThickness->value()));
}

// =============================================================================
// Selection computation helpers
// =============================================================================
QList<int> CustomSelectionDialog::expandBand(int centre, int thickness, int limit)
{
    QList<int> rows;
    if (thickness < 1 || limit <= 0) return rows;
    if (centre < 0 || centre >= limit) return rows;

    rows.append(centre);
    int up = centre - 1;
    int dn = centre + 1;
    bool goDown = false;  // alternate; start upward (above first)
    while (rows.size() < thickness) {
        bool placed = false;
        if (!goDown) {
            if (up >= 0)       { rows.append(up--);    placed = true; }
            else if (dn < limit){ rows.append(dn++);   placed = true; }
        } else {
            if (dn < limit)    { rows.append(dn++);    placed = true; }
            else if (up >= 0)  { rows.append(up--);    placed = true; }
        }
        if (!placed) break;
        goDown = !goDown;
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

QPair<int,int> CustomSelectionDialog::span(int centre, int length, int dir, int limit)
{
    int xMin = centre, xMax = centre;
    switch (dir) {
    case 0:  // Right / Down
        xMax = qMin(limit - 1, centre + length - 1);
        break;
    case 1:  // Left / Up
        xMin = qMax(0, centre - (length - 1));
        break;
    case 2: {  // Outward
        const int half  = (length - 1) / 2;
        const int extra = (length - 1) - half;
        xMin = qMax(0, centre - half);
        xMax = qMin(limit - 1, centre + extra);
        break;
    }
    }
    return { xMin, xMax };
}

QSet<int> CustomSelectionDialog::computeRandom() const
{
    QSet<int> out;
    if (!m_randEnable || !m_randEnable->isChecked()) return out;
    const int total = m_mbCols * m_mbRows;
    if (total <= 0) return out;
    const int n = (total * m_randPct->value()) / 100;
    if (n <= 0) return out;

    // Reservoir-ish: generate unique indices.
    // For small n relative to total, rejection sampling is fine;
    // for large n, generate a shuffled candidate list.
    if (n < total / 2) {
        auto* rng = QRandomGenerator::global();
        while (out.size() < n) {
            out.insert(int(rng->bounded(total)));
        }
    } else {
        QVector<int> pool(total);
        for (int i = 0; i < total; ++i) pool[i] = i;
        auto* rng = QRandomGenerator::global();
        for (int i = total - 1; i > 0; --i) {
            const int j = int(rng->bounded(i + 1));
            std::swap(pool[i], pool[j]);
        }
        for (int i = 0; i < n; ++i) out.insert(pool[i]);
    }
    return out;
}

QSet<int> CustomSelectionDialog::computeRows() const
{
    QSet<int> out;
    if (!m_rowEnable || !m_rowEnable->isChecked()) return out;

    const int startX    = m_rowStartX->value();
    const int startY    = m_rowStartY->value();
    const int length    = m_rowLength->value();
    const int dir       = m_rowDir->value();
    const int thickness = m_rowThickness->value();
    const int dups      = m_rowSeedDups->value();
    const int gap       = m_rowSeedGap->value();
    const int seedDir   = m_rowSeedDir->value();

    // Compute x-span for the row (same for all duplicate bands).
    const QPair<int,int> xs = span(startX, length, dir, m_mbCols);

    // Build list of band centre Y positions: original + duplicates.
    QList<int> centreYs;
    centreYs.append(startY);
    const int step = qMax(1, thickness + gap);
    if (dups > 0) {
        if (seedDir == 0) { // Down
            for (int k = 1; k <= dups; ++k) {
                const int y = startY + k * step;
                if (y >= 0 && y < m_mbRows) centreYs.append(y);
            }
        } else if (seedDir == 1) { // Up
            for (int k = 1; k <= dups; ++k) {
                const int y = startY - k * step;
                if (y >= 0 && y < m_mbRows) centreYs.append(y);
            }
        } else { // Outward, alternating up/down
            int placed = 0;
            int k = 1;
            while (placed < dups) {
                const int yUp = startY - k * step;
                const int yDn = startY + k * step;
                const bool upOk = (yUp >= 0 && yUp < m_mbRows);
                const bool dnOk = (yDn >= 0 && yDn < m_mbRows);
                if (!upOk && !dnOk) break;
                if (upOk && placed < dups) { centreYs.append(yUp); ++placed; }
                if (dnOk && placed < dups) { centreYs.append(yDn); ++placed; }
                ++k;
            }
        }
    }

    // For each band, expand thickness rows, then fill x-span.
    for (const int cy : centreYs) {
        const QList<int> yRows = expandBand(cy, thickness, m_mbRows);
        for (const int y : yRows) {
            for (int x = xs.first; x <= xs.second; ++x) {
                out.insert(y * m_mbCols + x);
            }
        }
    }
    return out;
}

QSet<int> CustomSelectionDialog::computeColumns() const
{
    QSet<int> out;
    if (!m_colEnable || !m_colEnable->isChecked()) return out;

    const int startX    = m_colStartX->value();
    const int startY    = m_colStartY->value();
    const int length    = m_colLength->value();
    const int dir       = m_colDir->value();
    const int thickness = m_colThickness->value();
    const int dups      = m_colSeedDups->value();
    const int gap       = m_colSeedGap->value();
    const int seedDir   = m_colSeedDir->value();

    // y-span for the column band (primary axis).
    const QPair<int,int> ys = span(startY, length, dir, m_mbRows);

    // Centre X positions: original + duplicates (perpendicular to band).
    QList<int> centreXs;
    centreXs.append(startX);
    const int step = qMax(1, thickness + gap);
    if (dups > 0) {
        if (seedDir == 0) { // Right
            for (int k = 1; k <= dups; ++k) {
                const int x = startX + k * step;
                if (x >= 0 && x < m_mbCols) centreXs.append(x);
            }
        } else if (seedDir == 1) { // Left
            for (int k = 1; k <= dups; ++k) {
                const int x = startX - k * step;
                if (x >= 0 && x < m_mbCols) centreXs.append(x);
            }
        } else { // Outward
            int placed = 0;
            int k = 1;
            while (placed < dups) {
                const int xL = startX - k * step;
                const int xR = startX + k * step;
                const bool lOk = (xL >= 0 && xL < m_mbCols);
                const bool rOk = (xR >= 0 && xR < m_mbCols);
                if (!lOk && !rOk) break;
                if (lOk && placed < dups) { centreXs.append(xL); ++placed; }
                if (rOk && placed < dups) { centreXs.append(xR); ++placed; }
                ++k;
            }
        }
    }

    for (const int cx : centreXs) {
        const QList<int> xCols = expandBand(cx, thickness, m_mbCols);
        for (const int x : xCols) {
            for (int y = ys.first; y <= ys.second; ++y) {
                out.insert(y * m_mbCols + x);
            }
        }
    }
    return out;
}

QSet<int> CustomSelectionDialog::computedSelection() const
{
    QSet<int> all;
    all |= computeRandom();
    all |= computeRows();
    all |= computeColumns();
    return all;
}

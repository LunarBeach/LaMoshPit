#include "GrowShrinkDialog.h"

#include "core/util/SelectionMorphology.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QPushButton>
#include <QDialogButtonBox>
#include <algorithm>

static const char* kDirNames[3] = { "Forward", "Backward", "Outward" };

GrowShrinkDialog::GrowShrinkDialog(int mbCols, int mbRows,
                                   int currentFrame, int totalFrames,
                                   SelectionGetter getBaseSelection,
                                   std::function<void(int)> previewCb,
                                   std::function<void()>    revertCb,
                                   QWidget* parent)
    : QDialog(parent),
      m_mbCols(mbCols), m_mbRows(mbRows),
      m_currentFrame(currentFrame), m_totalFrames(totalFrames),
      m_getSel(std::move(getBaseSelection)),
      m_previewCb(std::move(previewCb)),
      m_revertCb(std::move(revertCb))
{
    setWindowTitle("Grow / Shrink Selection");
    setModal(true);
    setMinimumWidth(500);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Bipolar grow/shrink on current selection.\n"
                "Left shrinks (islands floor at 1 MB each); right grows "
                "(caps when every MB is selected).\n"
                "Current frame: %1     Total frames: %2")
            .arg(m_currentFrame).arg(m_totalFrames), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // ── Frame range: direction + length ───────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        auto* lbl = new QLabel("Direction:", this);
        lbl->setFixedWidth(70);
        m_dialDir = new QDial(this);
        m_dialDir->setRange(0, 2);
        m_dialDir->setNotchesVisible(true);
        m_dialDir->setNotchTarget(3);
        m_dialDir->setWrapping(false);
        m_dialDir->setFixedSize(54, 54);
        m_dialDir->setValue(0);
        m_lblDir = new QLabel(kDirNames[0], this);
        m_lblDir->setFixedWidth(90);
        m_lblDir->setStyleSheet("color:#00ff88; font:bold 9pt 'Consolas';");
        row->addWidget(lbl);
        row->addWidget(m_dialDir);
        row->addWidget(m_lblDir);
        row->addStretch(1);
        root->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        auto* lbl = new QLabel("Length:", this);
        lbl->setFixedWidth(70);
        m_sliderLen = new QSlider(Qt::Horizontal, this);
        m_sliderLen->setMinimum(0);
        m_sliderLen->setValue(0);
        m_sliderLen->setFixedWidth(260);
        m_lblLen = new QLabel("0 (current only)", this);
        m_lblLen->setFixedWidth(110);
        row->addWidget(lbl);
        row->addWidget(m_sliderLen);
        row->addWidget(m_lblLen);
        row->addStretch(1);
        root->addLayout(row);
    }

    // ── Amount (bipolar) slider ───────────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        auto* lbl = new QLabel("Amount:", this);
        lbl->setFixedWidth(70);
        m_sliderAmt = new QSlider(Qt::Horizontal, this);
        m_sliderAmt->setRange(0, 0);    // real range set by recomputeRangeAndCaps()
        m_sliderAmt->setValue(0);
        m_sliderAmt->setFixedWidth(320);
        m_lblAmt = new QLabel("0", this);
        m_lblAmt->setFixedWidth(60);
        m_lblAmt->setStyleSheet("color:#00ff88; font:bold 9pt 'Consolas';");
        row->addWidget(lbl);
        row->addWidget(m_sliderAmt);
        row->addWidget(m_lblAmt);
        row->addStretch(1);
        root->addLayout(row);
    }

    m_lblLimits = new QLabel(this);
    m_lblLimits->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(m_lblLimits);

    m_lblPreview = new QLabel(this);
    m_lblPreview->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(m_lblPreview);

    // ── Button box — "Apply" instead of OK ────────────────────────────────
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_btnApply = m_btnBox->addButton("Apply", QDialogButtonBox::AcceptRole);
    root->addWidget(m_btnBox);

    connect(m_dialDir,   &QDial::valueChanged,
            this, &GrowShrinkDialog::onDirectionChanged);
    connect(m_sliderLen, &QSlider::valueChanged,
            this, &GrowShrinkDialog::onLengthChanged);
    connect(m_sliderAmt, &QSlider::valueChanged,
            this, &GrowShrinkDialog::onAmountChanged);
    connect(m_btnApply,  &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnBox,    &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateLengthRange();
    recomputeRangeAndCaps();
    updateAmountLabel();
    firePreview();
}

int GrowShrinkDialog::amount() const { return m_sliderAmt->value(); }

GrowShrinkDialog::Direction GrowShrinkDialog::direction() const {
    return static_cast<Direction>(m_dialDir->value());
}
int GrowShrinkDialog::length() const { return m_sliderLen->value(); }

QVector<int> GrowShrinkDialog::targetFrames() const {
    QVector<int> out;
    out.append(m_currentFrame);
    const int L = length();
    if (L <= 0) return out;
    switch (direction()) {
    case Forward:
        for (int i = 1; i <= L; ++i) {
            const int f = m_currentFrame + i;
            if (f < m_totalFrames) out.append(f);
        }
        break;
    case Backward:
        for (int i = 1; i <= L; ++i) {
            const int f = m_currentFrame - i;
            if (f >= 0) out.append(f);
        }
        break;
    case Outward:
        for (int i = 1; i <= L; ++i) {
            const int fF = m_currentFrame + i;
            const int fB = m_currentFrame - i;
            if (fF < m_totalFrames) out.append(fF);
            if (fB >= 0)            out.append(fB);
        }
        break;
    }
    return out;
}

void GrowShrinkDialog::onDirectionChanged(int v) {
    m_lblDir->setText(kDirNames[qBound(0, v, 2)]);
    updateLengthRange();
    recomputeRangeAndCaps();
    firePreview();
}

void GrowShrinkDialog::onLengthChanged(int v) {
    if (v == 0) m_lblLen->setText("0 (current only)");
    else        m_lblLen->setText(QString("%1 frame(s)").arg(v));
    recomputeRangeAndCaps();
    firePreview();
}

void GrowShrinkDialog::onAmountChanged(int /*v*/) {
    updateAmountLabel();
    firePreview();
}

void GrowShrinkDialog::updateLengthRange() {
    int maxLen = 0;
    switch (direction()) {
    case Forward:
        maxLen = qMax(0, m_totalFrames - 1 - m_currentFrame);
        break;
    case Backward:
        maxLen = qMax(0, m_currentFrame);
        break;
    case Outward:
        maxLen = qMax(0, qMin(m_currentFrame,
                              m_totalFrames - 1 - m_currentFrame));
        break;
    }
    m_sliderLen->setMaximum(maxLen);
    if (m_sliderLen->value() > maxLen) m_sliderLen->setValue(maxLen);
}

void GrowShrinkDialog::recomputeRangeAndCaps() {
    const QVector<int> frames = targetFrames();

    // Global caps: max morphology steps across all target frames so the
    // slider can fully exercise the most-extreme frame.  Frames that reach
    // their own floor earlier simply stay at that floor when applied.
    int gShrink = 0, gGrow = 0, curShrink = 0, curGrow = 0;
    for (int f : frames) {
        const QSet<int> base = m_getSel(f);
        const int s = SelectionMorphology::maxShrinkSteps(base, m_mbCols, m_mbRows);
        const int g = SelectionMorphology::maxGrowSteps  (base, m_mbCols, m_mbRows);
        gShrink = std::max(gShrink, s);
        gGrow   = std::max(gGrow,   g);
        if (f == m_currentFrame) { curShrink = s; curGrow = g; }
    }
    m_globalShrinkCap = gShrink;
    m_globalGrowCap   = gGrow;
    m_curShrinkCap    = curShrink;
    m_curGrowCap      = curGrow;

    const int oldVal = m_sliderAmt->value();
    const int newMin = -gShrink;
    const int newMax =  gGrow;

    m_sliderAmt->blockSignals(true);
    m_sliderAmt->setRange(newMin, newMax);
    m_sliderAmt->setValue(qBound(newMin, oldVal, newMax));
    m_sliderAmt->blockSignals(false);

    m_lblLimits->setText(
        QString("Across %1 frame(s): shrink up to %2, grow up to %3  "
                "(this frame: shrink %4, grow %5)")
            .arg(frames.size())
            .arg(gShrink).arg(gGrow)
            .arg(curShrink).arg(curGrow));
}

void GrowShrinkDialog::updateAmountLabel() {
    const int v = m_sliderAmt->value();
    QString txt;
    if (v == 0)      txt = "0 (no change)";
    else if (v < 0)  txt = QString("%1 (shrink)").arg(v);
    else             txt = QString("+%1 (grow)").arg(v);
    m_lblAmt->setText(txt);
}

void GrowShrinkDialog::firePreview() {
    if (m_previewCb) m_previewCb(m_sliderAmt->value());

    // Preview readout for the current frame: base count → projected count.
    const QSet<int> base = m_getSel(m_currentFrame);
    const QSet<int> projected =
        SelectionMorphology::apply(base, m_mbCols, m_mbRows,
                                   m_sliderAmt->value());
    m_lblPreview->setText(
        QString("Current frame preview: %1 \u2192 %2 MB(s)")
            .arg(base.size()).arg(projected.size()));
}

void GrowShrinkDialog::reject() {
    if (m_revertCb) m_revertCb();
    QDialog::reject();
}

#include "SeedDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSlider>
#include <QDial>

static const char* kDirNames[3]  = { "Forward", "Backward", "Outward" };
static const char* kModeNames[2] = { "Merge",   "Override" };

SeedDialog::SeedDialog(int currentFrame, int totalFrames, QWidget* parent)
    : QDialog(parent),
      m_currentFrame(currentFrame),
      m_totalFrames(totalFrames)
{
    setWindowTitle("Seed Selection");
    setModal(true);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Copy this frame's MB selection to neighbouring frames.\n"
                "Current frame: %1     Total frames: %2")
            .arg(m_currentFrame).arg(m_totalFrames), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // Direction dial — 3-way switch (Forward / Backward / Outward)
    auto* dirRow = new QHBoxLayout();
    dirRow->setSpacing(10);
    auto* dirLabel = new QLabel("Direction:", this);
    dirLabel->setFixedWidth(70);
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
    dirRow->addWidget(dirLabel);
    dirRow->addWidget(m_dialDir);
    dirRow->addWidget(m_lblDir);
    dirRow->addStretch(1);
    root->addLayout(dirRow);

    // Mode knob — 2-way (Merge / Override)
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(10);
    auto* modeLabel = new QLabel("Mode:", this);
    modeLabel->setFixedWidth(70);
    m_dialMode = new QDial(this);
    m_dialMode->setRange(0, 1);
    m_dialMode->setNotchesVisible(true);
    m_dialMode->setNotchTarget(2);
    m_dialMode->setWrapping(false);
    m_dialMode->setFixedSize(54, 54);
    m_dialMode->setValue(0);
    m_lblMode = new QLabel(kModeNames[0], this);
    m_lblMode->setFixedWidth(90);
    m_lblMode->setStyleSheet("color:#00ff88; font:bold 9pt 'Consolas';");
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(m_dialMode);
    modeRow->addWidget(m_lblMode);
    modeRow->addStretch(1);
    root->addLayout(modeRow);

    // Length slider — dynamically clamped to clip boundaries.
    auto* lenRow = new QHBoxLayout();
    lenRow->setSpacing(10);
    auto* lenLabel = new QLabel("Length:", this);
    lenLabel->setFixedWidth(70);
    m_sliderLen = new QSlider(Qt::Horizontal, this);
    m_sliderLen->setMinimum(1);
    m_sliderLen->setValue(1);
    m_sliderLen->setFixedWidth(260);
    m_lblLen = new QLabel("1 frame(s)", this);
    m_lblLen->setFixedWidth(90);
    lenRow->addWidget(lenLabel);
    lenRow->addWidget(m_sliderLen);
    lenRow->addWidget(m_lblLen);
    lenRow->addStretch(1);
    root->addLayout(lenRow);

    m_lblPreview = new QLabel(this);
    m_lblPreview->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(m_lblPreview);

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_dialDir,   &QDial::valueChanged,
            this, &SeedDialog::onDirectionChanged);
    connect(m_sliderLen, &QSlider::valueChanged,
            this, &SeedDialog::onLengthChanged);
    connect(m_dialMode,  &QDial::valueChanged,
            this, &SeedDialog::onModeChanged);

    updateLengthRange();
}

SeedDialog::Direction SeedDialog::direction() const {
    return static_cast<Direction>(m_dialDir->value());
}

SeedDialog::Mode SeedDialog::mode() const {
    return static_cast<Mode>(m_dialMode->value());
}

int SeedDialog::length() const { return m_sliderLen->value(); }

QVector<int> SeedDialog::targetFrames() const {
    QVector<int> out;
    const int L = length();
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

void SeedDialog::onDirectionChanged(int v) {
    m_lblDir->setText(kDirNames[qBound(0, v, 2)]);
    updateLengthRange();
}

void SeedDialog::onLengthChanged(int v) {
    m_lblLen->setText(QString("%1 frame(s)").arg(v));
    updatePreview();
}

void SeedDialog::onModeChanged(int v) {
    m_lblMode->setText(kModeNames[qBound(0, v, 1)]);
    updatePreview();
}

void SeedDialog::updateLengthRange() {
    int maxLen = 1;
    switch (direction()) {
    case Forward:
        maxLen = qMax(1, m_totalFrames - 1 - m_currentFrame);
        break;
    case Backward:
        maxLen = qMax(1, m_currentFrame);
        break;
    case Outward:
        maxLen = qMax(1, qMin(m_currentFrame,
                              m_totalFrames - 1 - m_currentFrame));
        break;
    }
    m_sliderLen->setMaximum(maxLen);
    if (m_sliderLen->value() > maxLen) m_sliderLen->setValue(maxLen);
    updatePreview();
}

void SeedDialog::updatePreview() {
    const int nTargets = targetFrames().size();
    const char* verb = (mode() == Override) ? "will REPLACE the selection on"
                                            : "will be merged into";
    m_lblPreview->setText(
        QString("%1 %2 frame(s).").arg(verb).arg(nTargets));
}

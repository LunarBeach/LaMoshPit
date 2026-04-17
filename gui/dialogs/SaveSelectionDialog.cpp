#include "SaveSelectionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QSlider>
#include <QLabel>
#include <QDial>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>

static const char* kDirNames[3] = { "Left", "Right", "Outward" };

SaveSelectionDialog::SaveSelectionDialog(int currentFrame, int totalFrames,
                                         QWidget* parent)
    : QDialog(parent),
      m_currentFrame(currentFrame),
      m_totalFrames(totalFrames)
{
    setWindowTitle("Save Selection");
    setModal(true);
    setMinimumWidth(460);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }"
                  "QLineEdit { background:#222; color:#ddd; border:1px solid #333;"
                  "  padding:3px; font:8pt 'Consolas'; }"
                  "QRadioButton { color:#ccc; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Save painted MB selection(s) as a shareable preset.\n"
                "Current frame: %1     Total frames: %2")
            .arg(m_currentFrame).arg(m_totalFrames), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // Name field
    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Preset name:", this);
        lbl->setFixedWidth(90);
        m_edName = new QLineEdit(this);
        m_edName->setPlaceholderText("unique name for this preset");
        row->addWidget(lbl);
        row->addWidget(m_edName, 1);
        root->addLayout(row);
    }

    // Scope radios
    m_rbThis  = new QRadioButton("This frame only", this);
    m_rbAll   = new QRadioButton("All frames in clip", this);
    m_rbRange = new QRadioButton("Frame range", this);
    m_rbThis->setChecked(true);
    root->addWidget(m_rbThis);
    root->addWidget(m_rbAll);
    root->addWidget(m_rbRange);

    // Frame-range subgroup (direction + length)
    m_rangeBox = new QGroupBox("Range", this);
    m_rangeBox->setStyleSheet(
        "QGroupBox { color:#ccc; font:bold 8pt 'Consolas'; "
        "border:1px solid #333; border-radius:4px; margin-top:6px; padding:8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left:8px; padding:0 4px; }");
    auto* rlay = new QVBoxLayout(m_rangeBox);
    rlay->setContentsMargins(10, 14, 10, 10);
    rlay->setSpacing(6);

    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Direction:", m_rangeBox);
        lbl->setFixedWidth(70);
        m_dialDir = new QDial(m_rangeBox);
        m_dialDir->setRange(0, 2);
        m_dialDir->setNotchesVisible(true);
        m_dialDir->setNotchTarget(3);
        m_dialDir->setWrapping(false);
        m_dialDir->setFixedSize(54, 54);
        m_lblDir = new QLabel(kDirNames[0], m_rangeBox);
        m_lblDir->setFixedWidth(90);
        m_lblDir->setStyleSheet("color:#00ff88; font:bold 9pt 'Consolas';");
        row->addWidget(lbl);
        row->addWidget(m_dialDir);
        row->addWidget(m_lblDir);
        row->addStretch(1);
        rlay->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Length:", m_rangeBox);
        lbl->setFixedWidth(70);
        m_sliderLen = new QSlider(Qt::Horizontal, m_rangeBox);
        m_sliderLen->setMinimum(1);
        m_sliderLen->setValue(1);
        m_sliderLen->setFixedWidth(260);
        m_lblLen = new QLabel("1 frame(s)", m_rangeBox);
        m_lblLen->setFixedWidth(80);
        row->addWidget(lbl);
        row->addWidget(m_sliderLen);
        row->addWidget(m_lblLen);
        row->addStretch(1);
        rlay->addLayout(row);
    }
    root->addWidget(m_rangeBox);

    m_btnBox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (auto* ok = m_btnBox->button(QDialogButtonBox::Save))
        ok->setText("Save");
    root->addWidget(m_btnBox);

    connect(m_rbThis,  &QRadioButton::toggled, this, &SaveSelectionDialog::onScopeChanged);
    connect(m_rbAll,   &QRadioButton::toggled, this, &SaveSelectionDialog::onScopeChanged);
    connect(m_rbRange, &QRadioButton::toggled, this, &SaveSelectionDialog::onScopeChanged);
    connect(m_dialDir,   &QDial::valueChanged,
            this, &SaveSelectionDialog::onDirectionChanged);
    connect(m_sliderLen, &QSlider::valueChanged,
            this, &SaveSelectionDialog::onLengthChanged);
    connect(m_edName,    &QLineEdit::textChanged,
            this, [this](const QString&){ updateAcceptEnabled(); });
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateLengthRange();
    updateRangeControlsEnabled();
    updateAcceptEnabled();
}

QString SaveSelectionDialog::presetName() const { return m_edName->text().trimmed(); }

SaveSelectionDialog::Scope SaveSelectionDialog::scope() const {
    if (m_rbAll->isChecked())   return AllFrames;
    if (m_rbRange->isChecked()) return FrameRange;
    return ThisFrame;
}

SaveSelectionDialog::Direction SaveSelectionDialog::direction() const {
    return static_cast<Direction>(m_dialDir->value());
}

int SaveSelectionDialog::rangeLength() const { return m_sliderLen->value(); }

QList<int> SaveSelectionDialog::frameRangeIndices() const {
    QList<int> out;
    switch (scope()) {
    case ThisFrame:
        out.append(m_currentFrame);
        break;
    case AllFrames:
        for (int i = 0; i < m_totalFrames; ++i) out.append(i);
        break;
    case FrameRange: {
        out.append(m_currentFrame);  // always include the anchor frame
        const int L = rangeLength();
        switch (direction()) {
        case Right:
            for (int i = 1; i <= L; ++i) {
                const int f = m_currentFrame + i;
                if (f < m_totalFrames) out.append(f);
            }
            break;
        case Left:
            for (int i = 1; i <= L; ++i) {
                const int f = m_currentFrame - i;
                if (f >= 0) out.append(f);
            }
            break;
        case Outward:
            for (int i = 1; i <= L; ++i) {
                const int fR = m_currentFrame + i;
                const int fL = m_currentFrame - i;
                if (fR < m_totalFrames) out.append(fR);
                if (fL >= 0)            out.append(fL);
            }
            break;
        }
        std::sort(out.begin(), out.end());
        break;
    }
    }
    return out;
}

void SaveSelectionDialog::onScopeChanged()
{
    updateRangeControlsEnabled();
    updateAcceptEnabled();
}

void SaveSelectionDialog::onDirectionChanged(int v)
{
    m_lblDir->setText(kDirNames[qBound(0, v, 2)]);
    updateLengthRange();
}

void SaveSelectionDialog::onLengthChanged(int v)
{
    m_lblLen->setText(QString("%1 frame(s)").arg(v));
}

void SaveSelectionDialog::updateLengthRange()
{
    int maxLen = 1;
    switch (direction()) {
    case Right:
        maxLen = qMax(1, m_totalFrames - 1 - m_currentFrame);
        break;
    case Left:
        maxLen = qMax(1, m_currentFrame);
        break;
    case Outward:
        maxLen = qMax(1, qMin(m_currentFrame,
                              m_totalFrames - 1 - m_currentFrame));
        break;
    }
    m_sliderLen->setMaximum(maxLen);
    if (m_sliderLen->value() > maxLen) m_sliderLen->setValue(maxLen);
    m_lblLen->setText(QString("%1 frame(s)").arg(m_sliderLen->value()));
}

void SaveSelectionDialog::updateRangeControlsEnabled()
{
    m_rangeBox->setEnabled(scope() == FrameRange);
}

void SaveSelectionDialog::updateAcceptEnabled()
{
    if (auto* ok = m_btnBox->button(QDialogButtonBox::Save))
        ok->setEnabled(!presetName().isEmpty());
}

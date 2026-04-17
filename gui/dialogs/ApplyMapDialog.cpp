#include "ApplyMapDialog.h"

#include "ImportSelectionMapDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>

static const char* kDirNames[3] = { "Forward", "Backward", "Outward" };

ApplyMapDialog::ApplyMapDialog(const QString& clipVideoPath,
                               const QString& projectMoshVideoFolder,
                               const QString& projectMapsDir,
                               int currentFrame,
                               int totalFrames,
                               QWidget* parent)
    : QDialog(parent),
      m_clipPath(clipVideoPath),
      m_moshVideoFolder(projectMoshVideoFolder),
      m_mapsDir(projectMapsDir),
      m_currentFrame(currentFrame),
      m_totalFrames(totalFrames)
{
    setWindowTitle("Apply Selection Map");
    setModal(true);
    setMinimumWidth(480);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }"
                  "QComboBox { background:#222; color:#ddd; "
                  "  border:1px solid #333; padding:3px; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Apply a selection map to a range of frames.\n"
                "Length 0 applies the map to the current frame only.\n"
                "Current frame: %1     Total frames: %2")
            .arg(m_currentFrame).arg(m_totalFrames), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // ── Map dropdown + Import button ──────────────────────────────────────
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* lbl = new QLabel("Map:", this);
        lbl->setFixedWidth(70);
        m_cmbMap   = new QComboBox(this);
        m_btnImport = new QPushButton("Import Map\u2026", this);
        row->addWidget(lbl);
        row->addWidget(m_cmbMap, 1);
        row->addWidget(m_btnImport);
        root->addLayout(row);
    }

    // ── Direction dial ────────────────────────────────────────────────────
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

    // ── Length slider ─────────────────────────────────────────────────────
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

    m_lblPreview = new QLabel(this);
    m_lblPreview->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(m_lblPreview);

    m_btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_btnBox);

    connect(m_dialDir,   &QDial::valueChanged,
            this, &ApplyMapDialog::onDirectionChanged);
    connect(m_sliderLen, &QSlider::valueChanged,
            this, &ApplyMapDialog::onLengthChanged);
    connect(m_btnImport, &QPushButton::clicked,
            this, &ApplyMapDialog::onImportMapClicked);
    connect(m_cmbMap,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateAcceptEnabled(); });
    connect(m_btnBox,    &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox,    &QDialogButtonBox::rejected, this, &QDialog::reject);

    reloadMaps();
    updateLengthRange();
    updateAcceptEnabled();
}

void ApplyMapDialog::reloadMaps()
{
    const QString keep = selectedMapPath();
    m_maps = SelectionMap::load(m_clipPath, m_mapsDir);

    m_cmbMap->blockSignals(true);
    m_cmbMap->clear();
    for (const SelectionMapEntry& e : m_maps) {
        m_cmbMap->addItem(e.name, e.absPath);
    }
    // Restore prior selection if still present.
    int restore = -1;
    for (int i = 0; i < m_cmbMap->count(); ++i) {
        if (m_cmbMap->itemData(i).toString() == keep) { restore = i; break; }
    }
    if (restore >= 0) m_cmbMap->setCurrentIndex(restore);
    else if (m_cmbMap->count() > 0) m_cmbMap->setCurrentIndex(0);
    m_cmbMap->blockSignals(false);

    if (m_maps.isEmpty()) {
        m_cmbMap->addItem("(no maps imported for this clip)");
        m_cmbMap->setCurrentIndex(0);
        m_cmbMap->setEnabled(false);
    } else {
        m_cmbMap->setEnabled(true);
    }
    updateAcceptEnabled();
}

QString ApplyMapDialog::selectedMapPath() const
{
    if (!m_cmbMap || m_maps.isEmpty()) return QString();
    const int idx = m_cmbMap->currentIndex();
    if (idx < 0 || idx >= m_maps.size()) return QString();
    return m_maps[idx].absPath;
}

ApplyMapDialog::Direction ApplyMapDialog::direction() const {
    return static_cast<Direction>(m_dialDir->value());
}
int ApplyMapDialog::length() const { return m_sliderLen->value(); }

QVector<int> ApplyMapDialog::targetFrames() const {
    QVector<int> out;
    out.append(m_currentFrame);  // current frame always included

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

void ApplyMapDialog::onDirectionChanged(int v) {
    m_lblDir->setText(kDirNames[qBound(0, v, 2)]);
    updateLengthRange();
}

void ApplyMapDialog::onLengthChanged(int v) {
    if (v == 0)
        m_lblLen->setText("0 (current only)");
    else
        m_lblLen->setText(QString("%1 frame(s)").arg(v));
    updatePreview();
}

void ApplyMapDialog::updateLengthRange() {
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
    updatePreview();
}

void ApplyMapDialog::updatePreview() {
    const int n = targetFrames().size();
    m_lblPreview->setText(
        QString("Map will be applied to %1 frame(s). "
                "Existing MB selections on those frames will be REPLACED.").arg(n));
}

void ApplyMapDialog::updateAcceptEnabled() {
    const bool canAccept = !m_maps.isEmpty() && !selectedMapPath().isEmpty();
    if (auto* ok = m_btnBox->button(QDialogButtonBox::Ok))
        ok->setEnabled(canAccept);
}

void ApplyMapDialog::onImportMapClicked()
{
    ImportSelectionMapDialog dlg(m_moshVideoFolder, m_mapsDir,
                                 m_clipPath, this);
    if (dlg.exec() == QDialog::Accepted) {
        reloadMaps();
    }
}

#include "LoadSelectionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QDial>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMessageBox>

static const char* kModeNames[2] = { "Merge", "Override" };

LoadSelectionDialog::LoadSelectionDialog(int mbCols, int mbRows,
                                         int currentFrame, int totalFrames,
                                         QWidget* parent)
    : QDialog(parent),
      m_mbCols(mbCols),
      m_mbRows(mbRows),
      m_currentFrame(currentFrame),
      m_totalFrames(totalFrames)
{
    setWindowTitle("Load Selection Preset");
    setModal(true);
    setMinimumWidth(500);
    setStyleSheet("QDialog { background:#1a1a1a; }"
                  "QLabel  { color:#ccc; font:8pt 'Consolas'; }"
                  "QCheckBox { color:#ccc; font:8pt 'Consolas'; }"
                  "QComboBox { background:#222; color:#ddd; "
                  "  border:1px solid #333; padding:3px; font:8pt 'Consolas'; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QString("Pick a saved preset or import one from a shared .json file.\n"
                "Grid required: %1\u00d7%2     Current frame: %3/%4")
            .arg(m_mbCols).arg(m_mbRows)
            .arg(m_currentFrame).arg(m_totalFrames), this);
    info->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(info);

    // Preset dropdown + Import button
    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Preset:", this);
        lbl->setFixedWidth(70);
        m_cmbPreset = new QComboBox(this);
        m_btnImport = new QPushButton("Load New Selection Preset\u2026", this);
        row->addWidget(lbl);
        row->addWidget(m_cmbPreset, 1);
        row->addWidget(m_btnImport);
        root->addLayout(row);
    }

    m_lblMeta = new QLabel("(no preset selected)", this);
    m_lblMeta->setStyleSheet("color:#888; font:7pt 'Consolas';");
    root->addWidget(m_lblMeta);

    // This-frame-only checkbox
    m_chkThis = new QCheckBox("This frame only (apply preset's first frame to current frame)", this);
    root->addWidget(m_chkThis);

    // Length slider
    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Clip length:", this);
        lbl->setFixedWidth(80);
        m_sliderLen = new QSlider(Qt::Horizontal, this);
        m_sliderLen->setMinimum(1);
        m_sliderLen->setMaximum(1);
        m_sliderLen->setValue(1);
        m_sliderLen->setFixedWidth(260);
        m_lblLen = new QLabel("1 frame(s)", this);
        m_lblLen->setFixedWidth(80);
        row->addWidget(lbl);
        row->addWidget(m_sliderLen);
        row->addWidget(m_lblLen);
        row->addStretch(1);
        root->addLayout(row);
    }

    // Mode knob (Merge / Override)
    {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel("Mode:", this);
        lbl->setFixedWidth(80);
        m_dialMode = new QDial(this);
        m_dialMode->setRange(0, 1);
        m_dialMode->setNotchesVisible(true);
        m_dialMode->setNotchTarget(2);
        m_dialMode->setWrapping(false);
        m_dialMode->setFixedSize(54, 54);
        m_lblMode = new QLabel(kModeNames[0], this);
        m_lblMode->setFixedWidth(90);
        m_lblMode->setStyleSheet("color:#00ff88; font:bold 9pt 'Consolas';");
        row->addWidget(lbl);
        row->addWidget(m_dialMode);
        row->addWidget(m_lblMode);
        row->addStretch(1);
        root->addLayout(row);
    }

    // Button box — "Apply Selection" instead of OK, to match user wording.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_btnApply = m_btnBox->addButton("Apply Selection",
                                     QDialogButtonBox::AcceptRole);
    root->addWidget(m_btnBox);

    connect(m_btnImport,  &QPushButton::clicked,
            this, &LoadSelectionDialog::onImportFile);
    connect(m_cmbPreset,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LoadSelectionDialog::onPresetChanged);
    connect(m_chkThis,    &QCheckBox::toggled,
            this, &LoadSelectionDialog::onThisFrameOnlyToggled);
    connect(m_sliderLen,  &QSlider::valueChanged,
            this, &LoadSelectionDialog::onLengthChanged);
    connect(m_dialMode,   &QDial::valueChanged,
            this, &LoadSelectionDialog::onModeChanged);
    connect(m_btnApply,   &QPushButton::clicked,
            this, &LoadSelectionDialog::onApplyClicked);
    connect(m_btnBox,     &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshPresetList();
    updateLengthRange();
    updateAcceptEnabled();
}

bool LoadSelectionDialog::thisFrameOnly() const { return m_chkThis->isChecked(); }
int  LoadSelectionDialog::clipLength()    const { return m_sliderLen->value(); }

LoadSelectionDialog::Mode LoadSelectionDialog::mode() const {
    return static_cast<Mode>(m_dialMode->value());
}

void LoadSelectionDialog::refreshPresetList()
{
    const QString keep = (m_cmbPreset->currentIndex() >= 0 &&
                          m_cmbPreset->currentIndex() < m_presetPaths.size())
                         ? m_presetPaths[m_cmbPreset->currentIndex()]
                         : QString();

    m_cmbPreset->blockSignals(true);
    m_cmbPreset->clear();
    m_presetPaths = SelectionPresetIO::listUserPresets();
    for (const QString& p : m_presetPaths) {
        m_cmbPreset->addItem(QFileInfo(p).completeBaseName(), p);
    }
    int restore = -1;
    for (int i = 0; i < m_presetPaths.size(); ++i) {
        if (m_presetPaths[i] == keep) { restore = i; break; }
    }
    if (restore >= 0) m_cmbPreset->setCurrentIndex(restore);
    else if (!m_presetPaths.isEmpty()) m_cmbPreset->setCurrentIndex(0);
    m_cmbPreset->blockSignals(false);

    if (m_presetPaths.isEmpty()) {
        m_cmbPreset->addItem("(no presets installed \u2014 import one)");
        m_cmbPreset->setEnabled(false);
        m_loaded = SelectionPreset{};
        m_lblMeta->setText("(no preset selected)");
    } else {
        m_cmbPreset->setEnabled(true);
    }
    onPresetChanged(m_cmbPreset->currentIndex());
}

void LoadSelectionDialog::onPresetChanged(int idx)
{
    m_loaded = SelectionPreset{};
    if (idx < 0 || idx >= m_presetPaths.size()) {
        m_lblMeta->setText("(no preset selected)");
        updateLengthRange();
        updateAcceptEnabled();
        return;
    }

    QString err;
    if (!SelectionPresetIO::load(m_presetPaths[idx], m_loaded, err)) {
        m_lblMeta->setText("Failed to load preset: " + err);
        m_lblMeta->setStyleSheet("color:#ff6666; font:7pt 'Consolas';");
        updateLengthRange();
        updateAcceptEnabled();
        return;
    }

    const bool gridOk = (m_loaded.mbCols == m_mbCols &&
                         m_loaded.mbRows == m_mbRows);
    const QString msg =
        QString("Preset: %1\u00d7%2 grid, %3 frame(s), %4 selection set(s)%5")
            .arg(m_loaded.mbCols).arg(m_loaded.mbRows)
            .arg(m_loaded.frameCount).arg(m_loaded.frames.size())
            .arg(gridOk ? QString() : QString("  \u2014 GRID MISMATCH"));
    m_lblMeta->setText(msg);
    m_lblMeta->setStyleSheet(
        gridOk ? "color:#00ff88; font:7pt 'Consolas';"
               : "color:#ff6666; font:7pt 'Consolas';");

    updateLengthRange();
    updateAcceptEnabled();
}

void LoadSelectionDialog::onThisFrameOnlyToggled(bool on)
{
    m_sliderLen->setEnabled(!on);
    m_lblLen   ->setEnabled(!on);
}

void LoadSelectionDialog::onLengthChanged(int v)
{
    m_lblLen->setText(QString("%1 frame(s)").arg(v));
}

void LoadSelectionDialog::onModeChanged(int v)
{
    m_lblMode->setText(kModeNames[qBound(0, v, 1)]);
}

void LoadSelectionDialog::updateLengthRange()
{
    int remaining = qMax(0, m_totalFrames - m_currentFrame);
    int presetLen = qMax(0, m_loaded.frameCount);
    int maxLen    = qMax(1, qMin(remaining, presetLen));
    m_sliderLen->blockSignals(true);
    m_sliderLen->setMaximum(maxLen);
    if (m_sliderLen->value() > maxLen) m_sliderLen->setValue(maxLen);
    if (m_sliderLen->value() < 1)      m_sliderLen->setValue(1);
    m_sliderLen->blockSignals(false);
    m_lblLen->setText(QString("%1 frame(s)").arg(m_sliderLen->value()));
}

void LoadSelectionDialog::updateAcceptEnabled()
{
    const bool hasPreset = (m_loaded.frameCount > 0);
    const bool gridOk    = hasPreset &&
                           m_loaded.mbCols == m_mbCols &&
                           m_loaded.mbRows == m_mbRows;
    m_btnApply->setEnabled(hasPreset && gridOk);
}

void LoadSelectionDialog::onImportFile()
{
    const QString picked = QFileDialog::getOpenFileName(
        this, "Import Selection Preset", QDir::homePath(),
        "Selection Preset (*.json);;All files (*.*)");
    if (picked.isEmpty()) return;

    // Validate before copying: must load as a valid preset.
    SelectionPreset probe;
    QString err;
    if (!SelectionPresetIO::load(picked, probe, err)) {
        QMessageBox::warning(this, "Import Selection Preset",
            "Not a valid selection preset:\n" + err);
        return;
    }

    // Copy into the user's preset folder with a de-duplicated name.
    const QString baseName = QFileInfo(picked).completeBaseName();
    QString target = SelectionPresetIO::userPresetPathFor(baseName);
    int n = 1;
    while (QFile::exists(target)) {
        target = SelectionPresetIO::userPresetPathFor(
            QString("%1_%2").arg(baseName).arg(n));
        ++n;
    }
    if (!QFile::copy(picked, target)) {
        QMessageBox::warning(this, "Import Selection Preset",
            "Could not copy preset into:\n" + target);
        return;
    }
    refreshPresetList();
    // Select the freshly-imported preset.
    const QString importedBase = QFileInfo(target).completeBaseName();
    for (int i = 0; i < m_cmbPreset->count(); ++i) {
        if (m_cmbPreset->itemText(i) == importedBase) {
            m_cmbPreset->setCurrentIndex(i);
            break;
        }
    }
}

void LoadSelectionDialog::onApplyClicked()
{
    if (m_loaded.frameCount <= 0) { reject(); return; }
    accept();
}

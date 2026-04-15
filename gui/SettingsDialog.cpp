#include "gui/SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QDialogButtonBox>

namespace {

// Centralised key name so the dialog writer and the MainWindow reader
// can't drift.
const char* kHwImportKey = "encode/useHwOnImport";

} // namespace

// Static helper — callable without instantiating the dialog.
bool SettingsDialog::importUsesHwEncode()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return s.value(kHwImportKey, false).toBool();
}

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("LaMoshPit Settings");
    setModal(true);
    setMinimumWidth(460);

    auto* outer = new QVBoxLayout(this);

    // ── Group: New Import Encode Settings ─────────────────────────────────
    auto* group = new QGroupBox("New Import Encode Settings", this);
    auto* grid  = new QVBoxLayout(group);

    m_chkHwImport = new QCheckBox(
        "Use hardware acceleration for new file imports", group);
    m_chkHwImport->setToolTip(
        "When enabled, imported videos are re-encoded using your GPU's "
        "hardware H.264 encoder (NVIDIA NVENC / AMD AMF / Intel QSV / "
        "Windows Media Foundation, whichever is available).\n\n"
        "Automatically falls back to CPU libx264 if no hardware encoder "
        "can be initialised on this machine.");

    auto* explainer = new QLabel(
        "<p style='color:#888; font-size:9pt;'>"
        "Hardware acceleration only applies to <b>new file imports</b> and "
        "to <b>NLE timeline renders</b> (selectable per-render in the "
        "Render dialog).<br><br>"
        "It deliberately does <b>not</b> affect mosh-editor operations "
        "(Force I/P/B, MB edits, Global Encode Params renders, Quick Mosh). "
        "Those always use the forked libx264 on the CPU because the per-MB "
        "bitstream-surgery hooks that make datamoshing work aren't available "
        "in GPU encoders."
        "</p>", group);
    explainer->setWordWrap(true);

    grid->addWidget(m_chkHwImport);
    grid->addWidget(explainer);

    outer->addWidget(group);
    outer->addStretch(1);

    // ── Buttons ──────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    // Load current values from persistent settings.
    m_chkHwImport->setChecked(importUsesHwEncode());
}

void SettingsDialog::onAccepted()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    s.setValue(kHwImportKey, m_chkHwImport->isChecked());
    accept();
}

#include "gui/SettingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QFileDialog>
#include <QSettings>
#include <QDialogButtonBox>

namespace {

// Centralised key names so the dialog writer and readers can't drift.
const char* kHwImportKey        = "encode/useHwOnImport";
const char* kSelColorKey        = "ui/selectionColor";
const char* kMoshFolderKey      = "paths/moshVideoFolderOverride";
const char* kMaxUndoMBKey       = "undo/maxStepsMBEditor";
const char* kMaxUndoSeqKey      = "undo/maxStepsSequencer";
constexpr int kMaxUndoDefault   = 50;
constexpr int kMaxUndoMin       = 10;
constexpr int kMaxUndoMax       = 500;

static int clampUndo(int v) {
    if (v < kMaxUndoMin) return kMaxUndoMin;
    if (v > kMaxUndoMax) return kMaxUndoMax;
    return v;
}

struct ColorChoice { const char* name; QColor rgb; };
// Order here is the order shown in the dropdown.  Yellow is first so it's
// the default when the preference is missing.
static const ColorChoice kChoices[] = {
    { "Yellow", QColor(255, 200,   0) },   // current default
    { "Green",  QColor(  0, 255, 120) },
    { "Red",    QColor(255,  60,  60) },
    { "Blue",   QColor( 64, 144, 255) },
    { "Pink",   QColor(255, 120, 200) },
    { "Purple", QColor(200, 100, 255) },
    { "Cyan",   QColor(  0, 230, 230) },
    { "Orange", QColor(255, 140,   0) },
};
constexpr int kChoiceCount = int(sizeof(kChoices) / sizeof(kChoices[0]));

static QColor colorByName(const QString& name) {
    for (const ColorChoice& c : kChoices) {
        if (name.compare(QLatin1String(c.name), Qt::CaseInsensitive) == 0)
            return c.rgb;
    }
    return kChoices[0].rgb;  // fall back to Yellow
}

} // namespace

// Static helper — callable without instantiating the dialog.
bool SettingsDialog::importUsesHwEncode()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return s.value(kHwImportKey, false).toBool();
}

QColor SettingsDialog::selectionOverlayColor()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return colorByName(s.value(kSelColorKey, "Yellow").toString());
}

QString SettingsDialog::moshVideoFolderOverride()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return s.value(kMoshFolderKey, QString()).toString();
}

int SettingsDialog::maxUndoStepsMBEditor()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return clampUndo(s.value(kMaxUndoMBKey, kMaxUndoDefault).toInt());
}

int SettingsDialog::maxUndoStepsSequencer()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    return clampUndo(s.value(kMaxUndoSeqKey, kMaxUndoDefault).toInt());
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

    // ── Group: Appearance ────────────────────────────────────────────────
    auto* appearanceGroup = new QGroupBox("Appearance", this);
    auto* appearanceLay   = new QVBoxLayout(appearanceGroup);

    auto* rowSel = new QHBoxLayout();
    auto* lblSel = new QLabel("MB selection overlay colour:", appearanceGroup);
    m_cmbSelColor = new QComboBox(appearanceGroup);
    m_cmbSelColor->setToolTip(
        "Colour used for the painted-macroblock overlay and the brush outline.\n"
        "Only the hue changes — the overlay opacity stays the same.");
    for (const ColorChoice& c : kChoices) {
        m_cmbSelColor->addItem(QLatin1String(c.name));
    }
    rowSel->addWidget(lblSel);
    rowSel->addWidget(m_cmbSelColor, 1);
    appearanceLay->addLayout(rowSel);

    outer->addWidget(appearanceGroup);

    // ── Group: Storage ───────────────────────────────────────────────────
    auto* storageGroup = new QGroupBox("Storage", this);
    auto* storageLay   = new QVBoxLayout(storageGroup);

    auto* rowFolder = new QHBoxLayout();
    auto* lblFolder = new QLabel("Video folder location:", storageGroup);
    m_edMoshFolder  = new QLineEdit(storageGroup);
    m_edMoshFolder->setPlaceholderText("(default: inside each project folder)");
    m_edMoshFolder->setToolTip(
        "Absolute path where imported and rendered videos are stored.\n"
        "Leave empty to keep videos inside each project's MoshVideoFolder/\n"
        "(portable, self-contained).  Set to an absolute path (e.g. an\n"
        "external drive) to use one shared video vault across all projects.");
    auto* btnBrowseMosh = new QPushButton("Browse\u2026", storageGroup);
    rowFolder->addWidget(lblFolder);
    rowFolder->addWidget(m_edMoshFolder, 1);
    rowFolder->addWidget(btnBrowseMosh);
    storageLay->addLayout(rowFolder);

    connect(btnBrowseMosh, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseMoshFolder);

    outer->addWidget(storageGroup);

    // ── Group: Editing (Undo/Redo) ───────────────────────────────────────
    // The MB editor and the NLE sequencer are two distinct workspaces with
    // independent undo stacks — mixing them in one timeline produces
    // surprising Ctrl+Z behaviour.  Two separate caps let each workspace's
    // history depth be tuned independently.
    auto* editingGroup = new QGroupBox("Editing", this);
    auto* editingLay   = new QVBoxLayout(editingGroup);

    auto* rowUndoMB = new QHBoxLayout();
    auto* lblUndoMB = new QLabel("Max MB editor undo steps:", editingGroup);
    m_spinMaxUndoMB = new QSpinBox(editingGroup);
    m_spinMaxUndoMB->setRange(kMaxUndoMin, kMaxUndoMax);
    m_spinMaxUndoMB->setSingleStep(10);
    m_spinMaxUndoMB->setToolTip(
        "Maximum number of steps retained on the mosh-editor undo stack.\n"
        "Covers clip switches, MB knob changes, and selection painting.\n"
        "Higher values use more memory; 50 is plenty for typical work.");
    rowUndoMB->addWidget(lblUndoMB);
    rowUndoMB->addWidget(m_spinMaxUndoMB);
    rowUndoMB->addStretch(1);
    editingLay->addLayout(rowUndoMB);

    auto* rowUndoSeq = new QHBoxLayout();
    auto* lblUndoSeq = new QLabel("Max NLE sequencer undo steps:", editingGroup);
    m_spinMaxUndoSeq = new QSpinBox(editingGroup);
    m_spinMaxUndoSeq->setRange(kMaxUndoMin, kMaxUndoMax);
    m_spinMaxUndoSeq->setSingleStep(10);
    m_spinMaxUndoSeq->setToolTip(
        "Maximum number of steps retained on the NLE sequencer undo stack.\n"
        "Covers track add/remove, clip add/move/trim/split, and loop edits.\n"
        "Sequencer undo is triggered by Ctrl+Z only when the sequencer dock\n"
        "has focus; otherwise Ctrl+Z walks the mosh-editor stack.");
    rowUndoSeq->addWidget(lblUndoSeq);
    rowUndoSeq->addWidget(m_spinMaxUndoSeq);
    rowUndoSeq->addStretch(1);
    editingLay->addLayout(rowUndoSeq);

    outer->addWidget(editingGroup);
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

    QSettings s("LaMoshPit", "LaMoshPit");
    const QString currentColor = s.value(kSelColorKey, "Yellow").toString();
    const int idx = m_cmbSelColor->findText(currentColor, Qt::MatchFixedString);
    m_cmbSelColor->setCurrentIndex(idx >= 0 ? idx : 0);

    m_edMoshFolder->setText(s.value(kMoshFolderKey, QString()).toString());
    m_spinMaxUndoMB->setValue(
        s.value(kMaxUndoMBKey, kMaxUndoDefault).toInt());
    m_spinMaxUndoSeq->setValue(
        s.value(kMaxUndoSeqKey, kMaxUndoDefault).toInt());
}

void SettingsDialog::onAccepted()
{
    QSettings s("LaMoshPit", "LaMoshPit");
    s.setValue(kHwImportKey,    m_chkHwImport->isChecked());
    s.setValue(kSelColorKey,    m_cmbSelColor->currentText());
    s.setValue(kMoshFolderKey,  m_edMoshFolder->text().trimmed());
    s.setValue(kMaxUndoMBKey,   m_spinMaxUndoMB->value());
    s.setValue(kMaxUndoSeqKey,  m_spinMaxUndoSeq->value());
    accept();
}

void SettingsDialog::onBrowseMoshFolder()
{
    const QString start = m_edMoshFolder->text().trimmed();
    const QString picked = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Choose video folder"),
        start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!picked.isEmpty())
        m_edMoshFolder->setText(picked);
}

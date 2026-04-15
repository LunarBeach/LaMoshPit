#include "gui/sequencer/SequencerRenderDialog.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QDateTime>
#include <QButtonGroup>

namespace sequencer {

SequencerRenderDialog::SequencerRenderDialog(const SequencerProject* project,
                                             int activeTrackIndex,
                                             bool loopRegionAvailable,
                                             Tick loopInTicks,
                                             Tick loopOutTicks,
                                             const QString& defaultOutputDir,
                                             QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_loopRegionAvailable(loopRegionAvailable && loopOutTicks > loopInTicks)
    , m_loopInTicks(loopInTicks)
    , m_loopOutTicks(loopOutTicks)
{
    setWindowTitle("Render NLE Sequence");
    setModal(true);

    auto* outer = new QVBoxLayout(this);

    // ── Source track ──────────────────────────────────────────────────────
    auto* form = new QFormLayout;
    m_trackCombo = new QComboBox(this);
    buildTrackList();
    if (activeTrackIndex >= 0 && activeTrackIndex < m_trackCombo->count())
        m_trackCombo->setCurrentIndex(activeTrackIndex);
    form->addRow("Source Track:", m_trackCombo);
    outer->addLayout(form);

    // ── Range ─────────────────────────────────────────────────────────────
    auto* rangeGroup = new QGroupBox("Range", this);
    auto* rangeLayout = new QVBoxLayout(rangeGroup);
    m_radioFullRange = new QRadioButton("Entire sequence", rangeGroup);
    m_radioLoopRange = new QRadioButton("Loop region (I/O markers)", rangeGroup);
    m_radioLoopRange->setEnabled(m_loopRegionAvailable);
    if (!m_loopRegionAvailable) {
        m_radioLoopRange->setToolTip(
            "Set Loop In (I) and Loop Out (O) markers in the transport "
            "before opening this dialog to enable this option.");
    }
    m_radioFullRange->setChecked(true);
    rangeLayout->addWidget(m_radioFullRange);
    rangeLayout->addWidget(m_radioLoopRange);
    outer->addWidget(rangeGroup);

    // ── Encoder ───────────────────────────────────────────────────────────
    auto* encGroup = new QGroupBox("Encoder", this);
    auto* encLayout = new QVBoxLayout(encGroup);
    m_radioEncDefault = new QRadioButton(
        "Default H.264 (libx264, CRF 18)", encGroup);
    m_radioEncGlobal  = new QRadioButton(
        "Use current Global Encode Params", encGroup);
    m_radioEncHw      = new QRadioButton(
        "Hardware accelerated (NVENC / AMF / QSV — auto-select)", encGroup);
    m_radioEncDefault->setChecked(true);

    // Group them explicitly so they're mutually exclusive.
    auto* encButtons = new QButtonGroup(this);
    encButtons->addButton(m_radioEncDefault);
    encButtons->addButton(m_radioEncGlobal);
    encButtons->addButton(m_radioEncHw);

    encLayout->addWidget(m_radioEncDefault);
    encLayout->addWidget(m_radioEncGlobal);
    encLayout->addWidget(m_radioEncHw);
    outer->addWidget(encGroup);

    // ── Destination ───────────────────────────────────────────────────────
    auto* destGroup = new QGroupBox("Destination", this);
    auto* destOuter = new QVBoxLayout(destGroup);
    auto* destRow   = new QHBoxLayout;
    m_destPath = new QLineEdit(destGroup);
    m_btnBrowse = new QPushButton("Browse\u2026", destGroup);

    // Seed a sensible default path: {defaultDir}/sequence_render_{timestamp}.mp4
    const QString defaultBase = defaultOutputDir.isEmpty()
        ? QDir::homePath()
        : defaultOutputDir;
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_destPath->setText(QDir(defaultBase).absoluteFilePath(
        QString("sequence_render_%1.mp4").arg(stamp)));

    destRow->addWidget(m_destPath, /*stretch=*/1);
    destRow->addWidget(m_btnBrowse);
    destOuter->addLayout(destRow);

    m_chkImportBack = new QCheckBox("Import into project after render", destGroup);
    m_chkImportBack->setChecked(true);
    destOuter->addWidget(m_chkImportBack);
    outer->addWidget(destGroup);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    m_btnCancel = new QPushButton("Cancel", this);
    m_btnRender = new QPushButton("Render", this);
    m_btnRender->setDefault(true);
    btnRow->addWidget(m_btnCancel);
    btnRow->addWidget(m_btnRender);
    outer->addLayout(btnRow);

    // Wiring.
    connect(m_btnBrowse, &QPushButton::clicked,
            this, &SequencerRenderDialog::onBrowseClicked);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &QDialog::reject);
    connect(m_btnRender, &QPushButton::clicked,
            this, &SequencerRenderDialog::onAcceptClicked);
}

void SequencerRenderDialog::buildTrackList()
{
    m_trackCombo->clear();
    if (!m_project) return;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        const auto& t = m_project->track(i);
        const int    nClips = t.clips.size();
        const QString label = QString("%1  (%2 clip%3)")
            .arg(t.name.isEmpty() ? QString("Track %1").arg(i + 1) : t.name)
            .arg(nClips)
            .arg(nClips == 1 ? "" : "s");
        m_trackCombo->addItem(label, i);
    }
}

void SequencerRenderDialog::onBrowseClicked()
{
    const QString start = m_destPath->text();
    const QString chosen = QFileDialog::getSaveFileName(
        this, "Save Render As", start, "MP4 Video (*.mp4)");
    if (!chosen.isEmpty()) {
        QString fixed = chosen;
        if (!fixed.endsWith(".mp4", Qt::CaseInsensitive)) fixed += ".mp4";
        m_destPath->setText(fixed);
    }
}

void SequencerRenderDialog::onAcceptClicked()
{
    const QString outPath = m_destPath->text().trimmed();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, "Render", "Please choose an output file.");
        return;
    }

    const int trackIdx = m_trackCombo->currentData().toInt();
    if (!m_project || trackIdx < 0 || trackIdx >= m_project->trackCount()) {
        QMessageBox::warning(this, "Render", "Invalid track selection.");
        return;
    }

    m_result.renderParams.outputPath      = outPath;
    m_result.renderParams.sourceTrackIndex = trackIdx;

    if (m_radioLoopRange->isChecked() && m_loopRegionAvailable) {
        m_result.renderParams.rangeStartTicks = m_loopInTicks;
        m_result.renderParams.rangeEndTicks   = m_loopOutTicks;
    } else {
        m_result.renderParams.rangeStartTicks = 0;
        m_result.renderParams.rangeEndTicks   = 0;   // 0 = full track
    }

    if (m_radioEncHw->isChecked())
        m_result.renderParams.encoderMode = SequencerRenderer::EncoderMode::Hardware;
    else if (m_radioEncGlobal->isChecked())
        m_result.renderParams.encoderMode = SequencerRenderer::EncoderMode::Libx264FromGlobal;
    else
        m_result.renderParams.encoderMode = SequencerRenderer::EncoderMode::Libx264Default;

    m_result.importToProject = m_chkImportBack->isChecked();

    accept();
}

} // namespace sequencer

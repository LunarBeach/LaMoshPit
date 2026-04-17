#include "gui/sequencer/SequencerClipPropertiesPanel.h"

#include "core/sequencer/ClipEffects.h"
#include "core/sequencer/Tick.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

namespace sequencer {

SequencerClipPropertiesPanel::SequencerClipPropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(4, 2, 4, 2);
    form->setSpacing(4);

    // Read-only identity rows.
    m_lblSource = new QLabel("—", this);
    m_lblSource->setWordWrap(true);
    m_lblSource->setMinimumWidth(160);
    form->addRow("Source:", m_lblSource);

    m_lblDuration = new QLabel("—", this);
    form->addRow("Duration:", m_lblDuration);

    // Opacity — slider + spinbox stay in sync, both feed onAnyKnobChanged.
    m_slOpacity = new QSlider(Qt::Horizontal, this);
    m_slOpacity->setRange(0, 100);
    m_slOpacity->setValue(100);
    m_sbOpacity = new QSpinBox(this);
    m_sbOpacity->setRange(0, 100);
    m_sbOpacity->setSuffix(" %");
    m_sbOpacity->setValue(100);
    auto* opRow = new QWidget(this);
    auto* opLay = new QHBoxLayout(opRow);
    opLay->setContentsMargins(0, 0, 0, 0);
    opLay->addWidget(m_slOpacity, 1);
    opLay->addWidget(m_sbOpacity);
    form->addRow("Opacity:", opRow);

    connect(m_slOpacity, &QSlider::valueChanged,
            this, [this](int v) {
                if (m_sbOpacity->value() != v) m_sbOpacity->setValue(v);
                onAnyKnobChanged();
            });
    connect(m_sbOpacity, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                if (m_slOpacity->value() != v) m_slOpacity->setValue(v);
                onAnyKnobChanged();
            });

    // Blend mode — enum order matches BlendMode ints for simple currentIndex
    // round-tripping.  Adding a new mode requires updating both ends.
    m_cbBlend = new QComboBox(this);
    m_cbBlend->addItem("Normal",   int(BlendMode::Normal));
    m_cbBlend->addItem("Multiply", int(BlendMode::Multiply));
    m_cbBlend->addItem("Screen",   int(BlendMode::Screen));
    m_cbBlend->addItem("Add",      int(BlendMode::Add));
    m_cbBlend->addItem("Overlay",  int(BlendMode::Overlay));
    m_cbBlend->setToolTip(
        "How this clip is composited onto lower tracks during render.\n"
        "Normal = alpha-over.  Multiply darkens, Screen brightens,\n"
        "Add clips at 255, Overlay emphasises contrast.");
    form->addRow("Blend:", m_cbBlend);
    connect(m_cbBlend, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SequencerClipPropertiesPanel::onAnyKnobChanged);

    // Fade in / fade out — measured in seconds, clamped at UI level to
    // half the clip's trimmed duration so in+out can't exceed the clip.
    m_dsbFadeIn = new QDoubleSpinBox(this);
    m_dsbFadeIn->setRange(0.0, 60.0);
    m_dsbFadeIn->setSingleStep(0.1);
    m_dsbFadeIn->setDecimals(2);
    m_dsbFadeIn->setSuffix(" s");
    m_dsbFadeIn->setToolTip(
        "Fade-in duration from the clip's trim start.  "
        "Render only — live VJ output is unaffected.");
    form->addRow("Fade in:", m_dsbFadeIn);

    m_dsbFadeOut = new QDoubleSpinBox(this);
    m_dsbFadeOut->setRange(0.0, 60.0);
    m_dsbFadeOut->setSingleStep(0.1);
    m_dsbFadeOut->setDecimals(2);
    m_dsbFadeOut->setSuffix(" s");
    m_dsbFadeOut->setToolTip(
        "Fade-out duration to the clip's trim end.  "
        "Render only — live VJ output is unaffected.");
    form->addRow("Fade out:", m_dsbFadeOut);

    connect(m_dsbFadeIn, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SequencerClipPropertiesPanel::onAnyKnobChanged);
    connect(m_dsbFadeOut, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SequencerClipPropertiesPanel::onAnyKnobChanged);

    // Applied effects — display-only list + Remove button.  Effects are
    // ADDED by dragging from the Effects Rack onto a clip in the timeline
    // (see SequencerTimelineView drop handling).  They're REMOVED here by
    // selecting an entry and clicking Remove.
    m_effectsList = new QListWidget(this);
    m_effectsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_effectsList->setMaximumHeight(72);
    m_effectsList->setToolTip(
        "Image effects applied to this clip before blending.\n"
        "Drop effects onto clips from the Effects Rack.");
    form->addRow("Effects:", m_effectsList);

    m_btnRemoveFx = new QPushButton("Remove Effect", this);
    m_btnRemoveFx->setEnabled(false);
    form->addRow("", m_btnRemoveFx);
    connect(m_effectsList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_btnRemoveFx->setEnabled(m_effectsList->currentRow() >= 0);
    });
    connect(m_btnRemoveFx, &QPushButton::clicked,
            this, &SequencerClipPropertiesPanel::onRemoveEffectClicked);

    // Start dimmed — no selection yet.
    setSelection(-1, -1, nullptr);
}

void SequencerClipPropertiesPanel::setSelection(int trackIndex,
                                                int clipIndex,
                                                const SequencerClip* clip)
{
    m_suppress = true;

    const bool haveClip = (clip != nullptr &&
                           trackIndex >= 0 && clipIndex >= 0);
    m_trackIndex        = haveClip ? trackIndex : -1;
    m_clipIndex         = haveClip ? clipIndex  : -1;
    m_clipDurationTicks = haveClip ? clip->trimmedDurationTicks() : 0;

    setEnabled(haveClip);

    if (!haveClip) {
        m_lblSource->setText("—");
        m_lblDuration->setText("—");
        m_slOpacity->setValue(100);
        m_sbOpacity->setValue(100);
        m_cbBlend->setCurrentIndex(0);
        m_dsbFadeIn->setValue(0.0);
        m_dsbFadeOut->setValue(0.0);
        m_currentEffects.clear();
        rebuildEffectsList();
        m_suppress = false;
        return;
    }

    m_lblSource->setText(QFileInfo(clip->sourcePath).fileName());
    m_lblSource->setToolTip(clip->sourcePath);
    const double durSec = ticksToSeconds(clip->trimmedDurationTicks());
    m_lblDuration->setText(QString::number(durSec, 'f', 2) + " s");

    const int op = int(clip->opacity * 100.0f + 0.5f);
    m_slOpacity->setValue(op);
    m_sbOpacity->setValue(op);

    const int blendIdx = m_cbBlend->findData(int(clip->blendMode));
    m_cbBlend->setCurrentIndex(blendIdx >= 0 ? blendIdx : 0);

    // Cap the spinboxes at the clip's duration so the user can't dial a
    // fade longer than the clip itself.  Cap at half so fade-in and
    // fade-out combined can't exceed duration without the envelope min()
    // squashing one of them silently.
    const double maxFade = std::max(0.0, durSec / 2.0);
    m_dsbFadeIn->setRange(0.0, maxFade);
    m_dsbFadeOut->setRange(0.0, maxFade);
    m_dsbFadeIn->setValue(ticksToSeconds(clip->fadeInTicks));
    m_dsbFadeOut->setValue(ticksToSeconds(clip->fadeOutTicks));

    m_currentEffects = clip->effects;
    rebuildEffectsList();

    m_suppress = false;
}

void SequencerClipPropertiesPanel::onAnyKnobChanged()
{
    emitIfLive();
}

void SequencerClipPropertiesPanel::emitIfLive()
{
    if (m_suppress) return;
    if (m_trackIndex < 0 || m_clipIndex < 0) return;

    const float opacity = float(m_sbOpacity->value()) / 100.0f;
    const BlendMode blend =
        BlendMode(m_cbBlend->currentData().toInt());
    const Tick fadeIn  = secondsToTicks(m_dsbFadeIn->value());
    const Tick fadeOut = secondsToTicks(m_dsbFadeOut->value());

    emit propertiesEdited(m_trackIndex, m_clipIndex,
                          opacity, blend, fadeIn, fadeOut);
}

void SequencerClipPropertiesPanel::rebuildEffectsList()
{
    if (!m_effectsList) return;
    m_effectsList->clear();
    for (ClipEffect e : m_currentEffects)
        m_effectsList->addItem(clipEffectDisplayName(e));
    m_btnRemoveFx->setEnabled(false);
}

void SequencerClipPropertiesPanel::onRemoveEffectClicked()
{
    if (m_suppress) return;
    if (m_trackIndex < 0 || m_clipIndex < 0) return;
    const int row = m_effectsList->currentRow();
    if (row < 0 || row >= m_currentEffects.size()) return;

    QVector<ClipEffect> next = m_currentEffects;
    next.remove(row);
    emit effectsEdited(m_trackIndex, m_clipIndex, std::move(next));
}

} // namespace sequencer

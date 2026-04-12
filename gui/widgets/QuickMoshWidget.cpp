#include "QuickMoshWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

// =============================================================================
// Preset table — displayed name + one-line description.
// The index here must match the switch in MainWindow::onQuickMosh.
// =============================================================================
struct QuickPresetMeta {
    const char* name;
    const char* desc;
};

static const QuickPresetMeta kMeta[] = {
    {
        "Ghost Smear",
        "Seeds frame 1 — ghostBlend copies the previous corrupted frame forward "
        "across the whole video, building an ever-present translucent echo trail."
    },
    {
        "MV Liquify \xe2\x86\x92",
        "Seeds frame 1 — motion-vector drift pushes all blocks rightward; "
        "amplified and cascaded for a horizontal liquid-smear throughout."
    },
    {
        "MV Liquify \xe2\x86\x93",
        "Seeds frame 1 — same as MV Liquify but drifting downward, "
        "gravity-melt look sustained across the full clip."
    },
    {
        "Chroma Bleed",
        "Seeds frame 1 — colour planes drift independently while luma holds; "
        "cascades a VHS-analogue colour bleed for the whole duration."
    },
    {
        "Vortex",
        "Seeds frame 1 — diagonal MV drift at 4\xc3\x97 amplify combined with "
        "ghost blend and noise; compound spiral corruption throughout."
    },
    {
        "Scatter Dissolve",
        "Seeds frame 1 — reference scatter + blurred ghost blend; each MB "
        "samples from random offsets creating a shattered-glass dissolve."
    },
    {
        "Full Freeze",
        "Seeds frame 1 — ghostBlend=100 + qpDelta=51 forces zero-residual "
        "skip MBs; encoder propagates the first frame verbatim forever."
    },
    {
        "Pixel Disintegrate",
        "Stamps every frame individually — heavy noise + DC offset + high QP "
        "erases fine detail and posterises the image across the whole clip."
    },
    {
        "UV Colour Twist",
        "Stamps every frame — independent U/V DC offsets twist hue across "
        "the clip; combine with Ghost Smear for a tinted smear trail."
    },
    {
        "Block Melt",
        "Stamps every frame — blockFlatten collapses each MB to its spatial "
        "average; encoder sees perfect blocks and produces heavy QP artefacts."
    },
};

static constexpr int kPresetCount = (int)(sizeof(kMeta) / sizeof(kMeta[0]));

// =============================================================================

QuickMoshWidget::QuickMoshWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // Header label
    auto* header = new QLabel("Quick Mosh", this);
    header->setStyleSheet(
        "font: bold 10pt 'Consolas'; color:#ff00ff; "
        "border-bottom: 1px solid #440044; padding-bottom:2px;");
    root->addWidget(header);

    // Combo + Mosh Now row
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    auto* comboLabel = new QLabel("Effect:", this);
    comboLabel->setStyleSheet("color:#888; font:9pt 'Consolas';");
    topRow->addWidget(comboLabel);

    m_combo = new QComboBox(this);
    for (int i = 0; i < kPresetCount; ++i)
        m_combo->addItem(kMeta[i].name);
    m_combo->setStyleSheet(
        "QComboBox { background:#1a1a1a; color:#ff88ff; border:1px solid #663366; "
        "font:bold 9pt 'Consolas'; padding:2px 6px; min-width:160px; }"
        "QComboBox::drop-down { border:none; width:20px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#ff88ff; "
        "selection-background-color:#441144; font:9pt 'Consolas'; }");
    topRow->addWidget(m_combo, 1);

    m_btnMosh = new QPushButton("Mosh Now!", this);
    m_btnMosh->setFixedHeight(32);
    m_btnMosh->setMinimumWidth(110);
    m_btnMosh->setEnabled(false);   // enabled once a video is loaded
    m_btnMosh->setStyleSheet(
        "QPushButton { background:#330033; color:#ff00ff; "
        "border:2px solid #ff00ff; border-radius:4px; font:bold 11pt 'Consolas'; }"
        "QPushButton:hover { background:#550055; color:#ffffff; border-color:#ffffff; }"
        "QPushButton:pressed { background:#ff00ff; color:#000000; }"
        "QPushButton:disabled { background:#1a1a1a; color:#443344; border-color:#332233; }");
    topRow->addWidget(m_btnMosh);
    root->addLayout(topRow);

    // Description label — auto-updates when combo changes
    m_desc = new QLabel(kMeta[0].desc, this);
    m_desc->setWordWrap(true);
    m_desc->setStyleSheet("color:#888; font:8pt 'Consolas'; padding:2px 0;");
    m_desc->setMinimumHeight(32);
    root->addWidget(m_desc);

    // Wire signals
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < kPresetCount)
            m_desc->setText(kMeta[idx].desc);
    });

    connect(m_btnMosh, &QPushButton::clicked, this, [this]() {
        emit moshRequested(m_combo->currentIndex());
    });
}

void QuickMoshWidget::setMoshEnabled(bool enabled)
{
    m_btnMosh->setEnabled(enabled);
}

#include "QuickMoshWidget.moc"

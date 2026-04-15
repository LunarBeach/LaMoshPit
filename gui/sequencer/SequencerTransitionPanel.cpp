#include "gui/sequencer/SequencerTransitionPanel.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QRandomGenerator>

namespace sequencer {

SequencerTransitionPanel::SequencerTransitionPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(4, 2, 4, 2);
    form->setSpacing(4);

    // Type row — dropdown + seed re-roll.
    m_typeCombo = new QComboBox(this);
    for (const auto& [id, name] : Transition::availableTypes()) {
        m_typeCombo->addItem(name, id);
    }
    m_seedBtn = new QPushButton("Re-roll", this);
    m_seedBtn->setToolTip("Generate a new random seed for MB Random.");

    auto* typeRow = new QWidget(this);
    auto* typeRowLayout = new QHBoxLayout(typeRow);
    typeRowLayout->setContentsMargins(0, 0, 0, 0);
    typeRowLayout->addWidget(m_typeCombo, /*stretch=*/1);
    typeRowLayout->addWidget(m_seedBtn);

    form->addRow("Type:", typeRow);

    // Duration.
    m_duration = new QDoubleSpinBox(this);
    m_duration->setRange(0.0, 10.0);
    m_duration->setSingleStep(0.1);
    m_duration->setDecimals(2);
    m_duration->setValue(1.0);
    m_duration->setSuffix(" s");
    form->addRow("Duration:", m_duration);

    // MB size.
    m_mbSize = new QSpinBox(this);
    m_mbSize->setRange(4, 128);
    m_mbSize->setSingleStep(4);
    m_mbSize->setValue(16);
    m_mbSize->setSuffix(" px");
    m_mbSize->setToolTip("Macroblock size for MB Random transition.");
    form->addRow("MB Size:", m_mbSize);

    // Curve.
    m_curveCombo = new QComboBox(this);
    m_curveCombo->addItem("Linear",   "linear");
    m_curveCombo->addItem("Ease In",  "ease_in");
    m_curveCombo->addItem("Ease Out", "ease_out");
    m_curveCombo->addItem("Smooth",   "smooth");
    form->addRow("Curve:", m_curveCombo);

    // Wiring.
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SequencerTransitionPanel::onTypeChanged);
    connect(m_duration, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SequencerTransitionPanel::onAnyParamChanged);
    connect(m_mbSize, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SequencerTransitionPanel::onAnyParamChanged);
    connect(m_curveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SequencerTransitionPanel::onAnyParamChanged);
    connect(m_seedBtn, &QPushButton::clicked,
            this, &SequencerTransitionPanel::onRerollSeed);
}

QString SequencerTransitionPanel::currentTypeId() const
{
    return m_typeCombo->currentData().toString();
}

TransitionParams SequencerTransitionPanel::currentParams() const
{
    TransitionParams p;
    p["duration_sec"] = m_duration->value();
    p["mb_size"]      = m_mbSize->value();
    p["curve"]        = m_curveCombo->currentData().toString();
    p["seed"]         = m_seed;
    return p;
}

void SequencerTransitionPanel::onTypeChanged(int /*idx*/)
{
    emit typeChanged(currentTypeId());
    emitParams();
}

void SequencerTransitionPanel::onAnyParamChanged()
{
    emitParams();
}

void SequencerTransitionPanel::onRerollSeed()
{
    // Pick a non-zero seed so Transition::create uses a deterministic
    // permutation for the next transition (0 means "randomize each run").
    do { m_seed = QRandomGenerator::global()->generate(); } while (m_seed == 0);
    emitParams();
}

void SequencerTransitionPanel::emitParams()
{
    emit paramsChanged(currentParams());
}

} // namespace sequencer

#pragma once

// =============================================================================
// SequencerTransitionPanel — live-editable transition type + params for the
// FrameRouter.
//
// Global-per-session policy: one transition is active at a time.  The user
// picks the type from a dropdown and tweaks params with spinboxes; the
// panel emits typeChanged / paramsChanged which the dock forwards to the
// FrameRouter.
//
// All params are shown regardless of which transition type is selected —
// irrelevant knobs (e.g. MB size on Crossfade) are silently ignored by the
// Transition subclass, and keeping a stable layout avoids UI reflow when
// the user swaps types.
// =============================================================================

#include "core/sequencer/Transition.h"
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

namespace sequencer {

class SequencerTransitionPanel : public QWidget {
    Q_OBJECT
public:
    explicit SequencerTransitionPanel(QWidget* parent = nullptr);

    QString          currentTypeId() const;
    TransitionParams currentParams() const;

signals:
    void typeChanged(const QString& typeId);
    void paramsChanged(const TransitionParams& params);

private slots:
    void onTypeChanged(int idx);
    void onAnyParamChanged();
    void onRerollSeed();

private:
    void emitParams();

    QComboBox*      m_typeCombo  { nullptr };
    QDoubleSpinBox* m_duration   { nullptr };
    QSpinBox*       m_mbSize     { nullptr };
    QComboBox*      m_curveCombo { nullptr };
    QPushButton*    m_seedBtn    { nullptr };

    uint32_t m_seed { 0 };   // 0 = randomize each run
};

} // namespace sequencer

#pragma once

#include <QWidget>

#include "core/model/GlobalEncodeParams.h"
#include "core/presets/PresetManager.h"

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class ToggleSwitch;

// =============================================================================
// GlobalParamsWidget
//
// Exposes ALL encoder-level prediction parameters for the entire encode pass.
// These are distinct from per-MB FrameMBParams knobs: they reshape the H.264
// encoder's fundamental decision-making (GOP structure, motion search strategy,
// partition mode, quantizer, psychovisual optimisation) across the whole file.
//
// Key workflow:
//   1. Pick a preset (or dial in custom values)
//   2. Click "Render" — re-encodes with these settings plus any active MB edits
// =============================================================================
class GlobalParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit GlobalParamsWidget(QWidget* parent = nullptr);

    // Returns the current parameters as configured in the UI.
    GlobalEncodeParams currentParams() const;

    // Push a params struct into the UI controls (e.g. after loading a preset).
    void setParams(const GlobalEncodeParams& p);

signals:
    void applyRequested();

private slots:
    void onPresetSelected(int idx);

    // User preset slots
    void onUserPresetSave();
    void onUserPresetDelete();
    void onUserPresetImport();

private:
    void refreshUserPresets();

private:
    QComboBox*      m_presetCombo;
    ToggleSwitch*   m_cbKillIFrames;   // prominent top-level toggle
    ToggleSwitch*   m_cbScenecut;

    // ── Frame structure
    QSpinBox*       m_sbGopSize;
    QSpinBox*       m_sbBFrames;
    QComboBox*      m_cbBAdapt;
    QSpinBox*       m_sbRefFrames;

    // ── Rate control
    QSpinBox*       m_sbQPOverride;
    QSpinBox*       m_sbQPMin;
    QSpinBox*       m_sbQPMax;

    // ── Motion estimation
    QComboBox*      m_cbMEMethod;
    QSpinBox*       m_sbMERange;
    QSpinBox*       m_sbSubpelRef;

    // ── Partitions / DCT  (three frame-type-specific MB Type dropdowns
    //     plus the 8×8 DCT toggle).  The old single m_cbPartition field
    //     was replaced by per-slice-type controls so users can restrict
    //     subdivision independently on I, P, and B frames.
    QComboBox*      m_cbIFrameMbType;
    QComboBox*      m_cbPFrameMbType;
    QComboBox*      m_cbBFrameMbType;
    ToggleSwitch*   m_cbx8x8DCT;

    // ── B-frame prediction
    QComboBox*      m_cbDirectMode;
    ToggleSwitch*   m_cbWeightedB;
    QComboBox*      m_cbWeightedP;

    // ── Quantization flags
    QComboBox*      m_cbTrellis;
    ToggleSwitch*   m_cbNoFastPSkip;
    ToggleSwitch*   m_cbNoDctDecimate;
    ToggleSwitch*   m_cbCabacDisable;

    // ── Deblocking
    ToggleSwitch*   m_cbNoDeblock;
    QSpinBox*       m_sbDeblockA;
    QSpinBox*       m_sbDeblockB;

    // ── Psychovisual
    QDoubleSpinBox* m_dsbPsyRD;
    QDoubleSpinBox* m_dsbPsyTrellis;
    QComboBox*      m_cbAQMode;
    QDoubleSpinBox* m_dsbAQStrength;
    ToggleSwitch*   m_cbMBTreeDisable;

    // ── Lookahead
    QSpinBox*       m_sbLookahead;

    // ── Rate-control fidelity
    ToggleSwitch*   m_cbQcompEnable;
    QDoubleSpinBox* m_dsbQcomp;
    ToggleSwitch*   m_cbIpratioEnable;
    QDoubleSpinBox* m_dsbIpratio;
    ToggleSwitch*   m_cbPbratioEnable;
    QDoubleSpinBox* m_dsbPbratio;
    ToggleSwitch*   m_cbDzInterEnable;
    QSpinBox*       m_sbDzInter;
    ToggleSwitch*   m_cbDzIntraEnable;
    QSpinBox*       m_sbDzIntra;
    ToggleSwitch*   m_cbQblurEnable;
    QDoubleSpinBox* m_dsbQblur;

    QPushButton*    m_btnApply;

    // ── Debug logging toggle
    ToggleSwitch*   m_cbDebugLog;

    // ── User preset buttons (presets live in the unified m_presetCombo)
    QPushButton* m_btnUserPresetSave { nullptr };
    QPushButton* m_btnUserPresetDel  { nullptr };
    QPushButton* m_btnUserPresetImport{ nullptr };
};

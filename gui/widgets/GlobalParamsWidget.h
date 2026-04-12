#pragma once

#include <QWidget>
#include <QSet>

#include "core/model/GlobalEncodeParams.h"

class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

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
//   2. Optionally capture the current MB painter selection as a "Spatial Mask"
//      — that selection is then stamped as a QP ROI on EVERY encoded frame
//   3. Click "Apply Global Params" (triggers MainWindow::onApplyMBEdits via signal)
// =============================================================================
class GlobalParamsWidget : public QWidget {
    Q_OBJECT
public:
    explicit GlobalParamsWidget(QWidget* parent = nullptr);

    // Returns the current parameters as configured in the UI.
    GlobalEncodeParams currentParams() const;

    // Push a params struct into the UI controls (e.g. after loading a preset).
    void setParams(const GlobalEncodeParams& p);

    // Called by MainWindow when the user paints MBs in the MB editor.
    // Stores the selection so it can be embedded as a spatial mask.
    void updateSpatialMask(const QSet<int>& mbs);

signals:
    // Emitted when the user clicks "Apply Global Params + Re-encode".
    void applyRequested();

private slots:
    void onPresetSelected(int idx);

private:
    QComboBox*      m_presetCombo;
    QCheckBox*      m_cbKillIFrames;  // prominent top-level checkbox

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

    // ── Partitions / DCT
    QComboBox*      m_cbPartition;
    QCheckBox*      m_cbx8x8DCT;

    // ── B-frame prediction
    QComboBox*      m_cbDirectMode;
    QCheckBox*      m_cbWeightedB;
    QComboBox*      m_cbWeightedP;

    // ── Quantization flags
    QComboBox*      m_cbTrellis;
    QCheckBox*      m_cbNoFastPSkip;
    QCheckBox*      m_cbNoDctDecimate;
    QCheckBox*      m_cbCabacDisable;

    // ── Deblocking
    QCheckBox*      m_cbNoDeblock;
    QSpinBox*       m_sbDeblockA;
    QSpinBox*       m_sbDeblockB;

    // ── Psychovisual
    QDoubleSpinBox* m_dsbPsyRD;
    QDoubleSpinBox* m_dsbPsyTrellis;
    QComboBox*      m_cbAQMode;
    QDoubleSpinBox* m_dsbAQStrength;
    QCheckBox*      m_cbMBTreeDisable;

    // ── Lookahead
    QSpinBox*       m_sbLookahead;

    // ── Spatial mask
    QLabel*         m_maskLabel;
    QSpinBox*       m_sbMaskQP;
    QPushButton*    m_btnCaptureMask;
    QSet<int>       m_spatialMask;

    QPushButton*    m_btnApply;

    // ── Debug logging toggle ───────────────────────────────────────────────────
    QCheckBox*      m_cbDebugLog;
};

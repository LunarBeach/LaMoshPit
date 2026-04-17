#pragma once

// =============================================================================
// SequencerClipPropertiesPanel — render-time properties editor for the
// currently-selected clip in the NLE timeline.
//
// Properties exposed (all offline-render-only; live FrameRouter ignores them):
//   - Opacity      (0 .. 100%)
//   - Blend mode   (Normal / Multiply / Screen / Add / Overlay)
//   - Fade in      (0 .. clip-length seconds)
//   - Fade out     (0 .. clip-length seconds)
//
// Above the editable knobs the panel shows the read-only clip identity:
// source filename and trimmed duration.
//
// Wiring pattern: SequencerTimelineView emits selectedClipChanged; the dock
// forwards to this panel's setSelection().  Every user edit emits one
// atomic propertiesEdited signal with the full new 4-tuple.  The dock
// converts that into a ChangeClipPropertyCmd and pushes onto the sequencer
// undo stack (session-scoped, separate from MB editor stack per Scope C).
//
// Programmatic push via setSelection() suppresses propertiesEdited so undo
// replays don't cause feedback into new command creation.
// =============================================================================

#include "core/sequencer/SequencerClip.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;

namespace sequencer {

class SequencerClipPropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SequencerClipPropertiesPanel(QWidget* parent = nullptr);

    // Populate from the clip at (trackIndex, clipIndex).  Pass clip=nullptr
    // (or invalid indices) to dim / disable the panel — used when the user
    // clicks empty space in the timeline or no clip is selected.
    void setSelection(int trackIndex, int clipIndex, const SequencerClip* clip);

signals:
    // Emitted on every user-initiated change.  The dock catches this and
    // wraps the 4-tuple in a ChangeClipPropertyCmd for the sequencer undo
    // stack.  Suppressed during setSelection() so undo replays don't feed
    // back into new command creation.
    void propertiesEdited(int trackIndex, int clipIndex,
                          float opacity, ::sequencer::BlendMode blend,
                          ::sequencer::Tick fadeInTicks,
                          ::sequencer::Tick fadeOutTicks);

    // Emitted when the user mutates the effects list (remove button).
    // The dock wraps this in a ChangeClipEffectsCmd.  Suppressed during
    // setSelection() for the same reason as propertiesEdited.
    void effectsEdited(int trackIndex, int clipIndex,
                       QVector<::sequencer::ClipEffect> newEffects);

private slots:
    void onAnyKnobChanged();
    void onRemoveEffectClicked();

private:
    void emitIfLive();
    void rebuildEffectsList();

    // Cache the selection so a knob emission can round-trip (trackIdx,
    // clipIdx) without having to search the project.
    int  m_trackIndex { -1 };
    int  m_clipIndex  { -1 };
    Tick m_clipDurationTicks { 0 };
    // Output frame rate — used to convert seconds ↔ ticks in the fade
    // spinboxes' UI representation.  The panel reads it from the
    // surrounding project on each setSelection so it stays in sync with
    // project changes.  Defaults to 30 fps if the project hasn't weighed in.
    double m_outputFps { 30.0 };

    // Suppression flag — true while setSelection is programmatically
    // populating the widgets.  Prevents those setValues from firing
    // propertiesEdited (which would create spurious undo commands during
    // redo/undo replays).
    bool m_suppress { false };

    // ── Widgets ───────────────────────────────────────────────────────────
    QLabel*         m_lblSource    { nullptr };
    QLabel*         m_lblDuration  { nullptr };
    QSlider*        m_slOpacity    { nullptr };
    QSpinBox*       m_sbOpacity    { nullptr };
    QComboBox*      m_cbBlend      { nullptr };
    QDoubleSpinBox* m_dsbFadeIn    { nullptr };
    QDoubleSpinBox* m_dsbFadeOut   { nullptr };
    QListWidget*    m_effectsList  { nullptr };
    QPushButton*    m_btnRemoveFx  { nullptr };

    // Cached current-clip effects so onRemoveEffectClicked has the full
    // list to emit minus the removed entry (avoids re-fetching through the
    // project).
    QVector<ClipEffect> m_currentEffects;
};

} // namespace sequencer

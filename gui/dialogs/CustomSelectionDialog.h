#pragma once

#include <QDialog>
#include <QSet>

class QSlider;
class QLabel;
class QDial;
class QCheckBox;
class QGroupBox;

// =============================================================================
// CustomSelectionDialog
//
// Quick-selection toolkit for the current frame.  Three independently-
// enablable sections, unioned together on accept:
//
//   1. RANDOM   — pick P % of the frame's MBs at random.
//   2. ROWS     — horizontal band of MBs with optional row-seed duplicates.
//   3. COLUMNS  — vertical band of MBs with optional column-seed duplicates.
//
// All sliders dynamically re-clamp their own (and dependent sliders') max
// ranges so the generated selection can never exceed frame boundaries.
//
// For bands:
//   • "start"      is the centre macroblock of the band.
//   • "length"     is the band extent along its primary axis.
//   • "direction"  is a 3-way knob — Right/Left/Outward for rows,
//                   Down/Up/Outward for columns.
//   • "thickness"  is the band width across the secondary axis.  Grows from
//                   start in an alternating outward pattern, clipped at the
//                   frame edge (if blocked on one side it keeps growing on
//                   the other until thickness is reached or both sides hit
//                   their boundary).
//   • "seed"       duplicates the band along the secondary axis at (gap)
//                   intervals in the chosen seed direction.
// =============================================================================
class CustomSelectionDialog : public QDialog {
    Q_OBJECT
public:
    CustomSelectionDialog(int mbCols, int mbRows,
                          const QSet<int>& existingSelection,
                          QWidget* parent = nullptr);

    // Union of all enabled sections' computed MBs.  Does NOT include the
    // existing selection passed in the constructor — caller unions manually.
    QSet<int> computedSelection() const;

signals:
    // Live preview of the merged selection (existing + computed) as the
    // user drags sliders / toggles sections. MacroblockWidget connects this
    // to the canvas so the grid updates in real time. Emitted on every
    // relevant control change; the caller is expected to restore the
    // original selection if the dialog is cancelled.
    void selectionPreview(const QSet<int>& mergedPreview);

private slots:
    void refreshRowRanges();
    void refreshColRanges();
    // Recomputes the merged selection and emits selectionPreview.
    void emitPreview();

private:
    // ── Section builders ──────────────────────────────────────────────────
    QGroupBox* buildRandomGroup();
    QGroupBox* buildRowsGroup();
    QGroupBox* buildColsGroup();

    // ── Selection computation helpers ─────────────────────────────────────
    QSet<int> computeRandom()  const;
    QSet<int> computeRows()    const;
    QSet<int> computeColumns() const;

    // Row-band helper: given centre Y and thickness, return the set of
    // row indices occupied (alternating up/down, clipped at edges).
    static QList<int> expandBand(int centre, int thickness, int limit);

    // Row span helper: given centre X, length, direction, mbCols,
    // return [xMin, xMax] inclusive.  Direction 0=Right/Down, 1=Left/Up, 2=Out.
    static QPair<int,int> span(int centre, int length, int dir, int limit);

    int m_mbCols = 0;
    int m_mbRows = 0;
    QSet<int> m_existing;

    // ── RANDOM section ────────────────────────────────────────────────────
    QCheckBox* m_randEnable  { nullptr };
    QSlider*   m_randPct     { nullptr };
    QLabel*    m_randPctLbl  { nullptr };

    // ── ROWS section ──────────────────────────────────────────────────────
    QCheckBox* m_rowEnable        { nullptr };
    QSlider*   m_rowThickness     { nullptr };  // 1..mbRows
    QLabel*    m_rowThicknessLbl  { nullptr };
    QSlider*   m_rowLength        { nullptr };  // 1..dynamic
    QLabel*    m_rowLengthLbl     { nullptr };
    QSlider*   m_rowStartX        { nullptr };  // 0..mbCols-1
    QLabel*    m_rowStartXLbl     { nullptr };
    QSlider*   m_rowStartY        { nullptr };  // 0..mbRows-1
    QLabel*    m_rowStartYLbl     { nullptr };
    QDial*     m_rowDir           { nullptr };  // 0=Right 1=Left 2=Out
    QLabel*    m_rowDirLbl        { nullptr };
    // Row seed
    QSlider*   m_rowSeedDups      { nullptr };  // 0..dynamic
    QLabel*    m_rowSeedDupsLbl   { nullptr };
    QSlider*   m_rowSeedGap       { nullptr };  // 0..mbRows-1
    QLabel*    m_rowSeedGapLbl    { nullptr };
    QDial*     m_rowSeedDir       { nullptr };  // 0=Down 1=Up 2=Out
    QLabel*    m_rowSeedDirLbl    { nullptr };

    // ── COLUMNS section ───────────────────────────────────────────────────
    QCheckBox* m_colEnable        { nullptr };
    QSlider*   m_colThickness     { nullptr };  // 1..mbCols
    QLabel*    m_colThicknessLbl  { nullptr };
    QSlider*   m_colLength        { nullptr };  // 1..dynamic
    QLabel*    m_colLengthLbl     { nullptr };
    QSlider*   m_colStartX        { nullptr };
    QLabel*    m_colStartXLbl     { nullptr };
    QSlider*   m_colStartY        { nullptr };
    QLabel*    m_colStartYLbl     { nullptr };
    QDial*     m_colDir           { nullptr };  // 0=Down 1=Up 2=Out
    QLabel*    m_colDirLbl        { nullptr };
    // Column seed
    QSlider*   m_colSeedDups      { nullptr };
    QLabel*    m_colSeedDupsLbl   { nullptr };
    QSlider*   m_colSeedGap       { nullptr };
    QLabel*    m_colSeedGapLbl    { nullptr };
    QDial*     m_colSeedDir       { nullptr };  // 0=Right 1=Left 2=Out
    QLabel*    m_colSeedDirLbl    { nullptr };
};

#pragma once

#include <QSet>
#include <QList>

// =============================================================================
// SelectionMorphology
//
// Pure helpers for eroding / dilating painted MB selections on a 2D grid.
// Used by the Grow/Shrink feature.
//
// An "island" is a 4-connected component of selected MBs.  Erosion pins
// every island at a minimum size of ONE macroblock — if a standard erosion
// step would annihilate an island, the island's median-indexed MB is
// preserved instead so the user's leftmost slider position is exactly "all
// islands reduced to a single MB each".
//
// Indices follow the project convention: idx = row * mbCols + col.
// =============================================================================
class SelectionMorphology {
public:
    // Split `sel` into 4-connected components.
    static QList<QSet<int>> connectedComponents(const QSet<int>& sel,
                                                int mbCols, int mbRows);

    // One erosion step with single-MB-island preservation.
    static QSet<int> erodeStep(const QSet<int>& sel,
                               int mbCols, int mbRows);

    // One dilation step (adds 4-neighbour MBs), bounded by grid.
    static QSet<int> dilateStep(const QSet<int>& sel,
                                int mbCols, int mbRows);

    // Apply `steps` morphology rounds to `base`.  Positive = dilate,
    // negative = erode.  Zero returns a copy of base.
    static QSet<int> apply(const QSet<int>& base,
                           int mbCols, int mbRows,
                           int steps);

    // How many erosion steps until every island in `sel` is exactly 1 MB.
    // Returns 0 if the input is already all single-MB islands (or empty).
    static int maxShrinkSteps(const QSet<int>& sel,
                              int mbCols, int mbRows);

    // How many dilation steps until every MB in the grid is selected.
    // Returns 0 if `sel` is empty (nothing to dilate from) or already full.
    static int maxGrowSteps(const QSet<int>& sel,
                            int mbCols, int mbRows);
};

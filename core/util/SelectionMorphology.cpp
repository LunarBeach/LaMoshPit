#include "SelectionMorphology.h"

#include <QQueue>
#include <algorithm>

namespace {

struct Delta { int dr; int dc; };
static constexpr Delta kNeighbours4[4] = {
    {-1, 0}, {1, 0}, {0, -1}, {0, 1}
};

inline bool inBounds(int r, int c, int rows, int cols) {
    return r >= 0 && r < rows && c >= 0 && c < cols;
}

}  // namespace

QList<QSet<int>> SelectionMorphology::connectedComponents(
    const QSet<int>& sel, int mbCols, int mbRows)
{
    QList<QSet<int>> out;
    if (sel.isEmpty() || mbCols <= 0 || mbRows <= 0) return out;

    QSet<int> remaining = sel;
    while (!remaining.isEmpty()) {
        const int seed = *remaining.begin();
        QSet<int> island;
        QQueue<int> q;
        q.enqueue(seed);
        island.insert(seed);
        remaining.remove(seed);

        while (!q.isEmpty()) {
            const int idx = q.dequeue();
            const int r = idx / mbCols;
            const int c = idx % mbCols;
            for (const Delta& d : kNeighbours4) {
                const int nr = r + d.dr, nc = c + d.dc;
                if (!inBounds(nr, nc, mbRows, mbCols)) continue;
                const int nidx = nr * mbCols + nc;
                if (!remaining.contains(nidx)) continue;
                remaining.remove(nidx);
                island.insert(nidx);
                q.enqueue(nidx);
            }
        }
        out.append(std::move(island));
    }
    return out;
}

QSet<int> SelectionMorphology::erodeStep(const QSet<int>& sel,
                                         int mbCols, int mbRows)
{
    if (sel.isEmpty() || mbCols <= 0 || mbRows <= 0) return sel;

    QList<QSet<int>> islands = connectedComponents(sel, mbCols, mbRows);
    QSet<int> out;
    for (const QSet<int>& island : islands) {
        if (island.size() <= 1) {
            out |= island;
            continue;
        }
        // Standard erosion restricted to this island: keep MBs whose four
        // 4-neighbours are all inside the island.
        QSet<int> eroded;
        for (int idx : island) {
            const int r = idx / mbCols;
            const int c = idx % mbCols;
            bool keep = true;
            for (const Delta& d : kNeighbours4) {
                const int nr = r + d.dr, nc = c + d.dc;
                if (!inBounds(nr, nc, mbRows, mbCols)) { keep = false; break; }
                if (!island.contains(nr * mbCols + nc)) { keep = false; break; }
            }
            if (keep) eroded.insert(idx);
        }
        if (eroded.isEmpty()) {
            // Island would vanish — pin a single representative so the user's
            // leftmost slider position is exactly "every island is 1 MB".
            // Median-by-index is stable + deterministic.
            QList<int> sorted = island.values();
            std::sort(sorted.begin(), sorted.end());
            out.insert(sorted[sorted.size() / 2]);
        } else {
            out |= eroded;
        }
    }
    return out;
}

QSet<int> SelectionMorphology::dilateStep(const QSet<int>& sel,
                                          int mbCols, int mbRows)
{
    if (sel.isEmpty() || mbCols <= 0 || mbRows <= 0) return sel;

    QSet<int> out = sel;
    for (int idx : sel) {
        const int r = idx / mbCols;
        const int c = idx % mbCols;
        for (const Delta& d : kNeighbours4) {
            const int nr = r + d.dr, nc = c + d.dc;
            if (!inBounds(nr, nc, mbRows, mbCols)) continue;
            out.insert(nr * mbCols + nc);
        }
    }
    return out;
}

QSet<int> SelectionMorphology::apply(const QSet<int>& base,
                                     int mbCols, int mbRows,
                                     int steps)
{
    if (steps == 0 || base.isEmpty()) return base;
    QSet<int> cur = base;
    if (steps > 0) {
        const int total = mbCols * mbRows;
        for (int i = 0; i < steps; ++i) {
            if (cur.size() >= total) break;
            QSet<int> next = dilateStep(cur, mbCols, mbRows);
            if (next.size() == cur.size()) break;  // can't grow further
            cur = std::move(next);
        }
    } else {
        for (int i = 0; i < -steps; ++i) {
            QSet<int> next = erodeStep(cur, mbCols, mbRows);
            if (next == cur) break;  // all islands are single MBs
            cur = std::move(next);
        }
    }
    return cur;
}

int SelectionMorphology::maxShrinkSteps(const QSet<int>& sel,
                                        int mbCols, int mbRows)
{
    if (sel.isEmpty()) return 0;
    QSet<int> cur = sel;
    int steps = 0;
    // Safety cap: grid diameter bounds the answer comfortably.
    const int cap = mbCols + mbRows;
    while (steps < cap) {
        QSet<int> next = erodeStep(cur, mbCols, mbRows);
        if (next == cur) break;
        cur = std::move(next);
        ++steps;
    }
    return steps;
}

int SelectionMorphology::maxGrowSteps(const QSet<int>& sel,
                                      int mbCols, int mbRows)
{
    if (sel.isEmpty()) return 0;
    const int total = mbCols * mbRows;
    if (sel.size() >= total) return 0;
    QSet<int> cur = sel;
    int steps = 0;
    const int cap = mbCols + mbRows;
    while (steps < cap && cur.size() < total) {
        QSet<int> next = dilateStep(cur, mbCols, mbRows);
        if (next.size() == cur.size()) break;
        cur = std::move(next);
        ++steps;
    }
    return steps;
}

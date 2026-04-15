#include "core/sequencer/Transition.h"

#include <QPainter>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sequencer {

// =============================================================================
// HardCutTransition — instant swap.  Serves as the "no effect" default.
// =============================================================================

class HardCutTransition : public Transition {
public:
    QString typeId()      const override { return "hard_cut"; }
    QString displayName() const override { return "Hard Cut"; }

    QImage compose(const QImage& outgoing, const QImage& incoming,
                   double progress, const TransitionParams&) override
    {
        // Flip to incoming at the first frame the router sets progress > 0.
        return (progress > 0.0) ? incoming : outgoing;
    }
};

// =============================================================================
// CrossfadeTransition — alpha blend between outgoing and incoming.
// =============================================================================

class CrossfadeTransition : public Transition {
public:
    QString typeId()      const override { return "crossfade"; }
    QString displayName() const override { return "Crossfade"; }

    QImage compose(const QImage& outgoing, const QImage& incoming,
                   double progress, const TransitionParams& params) override
    {
        const QString curve = params.value("curve", "linear").toString();
        const double  t     = applyCurve(progress, curve);
        QImage out = outgoing.copy();
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setOpacity(t);
        p.drawImage(0, 0, incoming);
        return out;
    }
};

// =============================================================================
// MBRandomTransition — sporadic per-macroblock switch from outgoing to
// incoming.  Macroblock size and the permutation order are the "character"
// knobs; the curve shapes how MBs activate over time.
// =============================================================================

class MBRandomTransition : public Transition {
public:
    QString typeId()      const override { return "mb_random"; }
    QString displayName() const override { return "MB Random"; }

    void start(int /*fromTrack*/, int /*toTrack*/,
               const TransitionParams& params) override
    {
        const uint32_t seed = params.value("seed", 0u).toUInt();
        if (seed != 0) m_rng.seed(seed);
        else           m_rng.seed(QRandomGenerator::global()->generate());
        // The actual permutation is regenerated on the first compose() call
        // when the frame size becomes known.
        m_perm.clear();
        m_permW = m_permH = m_permMBSize = 0;
    }

    QImage compose(const QImage& outgoing, const QImage& incoming,
                   double progress, const TransitionParams& params) override
    {
        const int mbSize = std::max(4, params.value("mb_size", 16).toInt());
        const QString curve = params.value("curve", "linear").toString();
        const double  t     = applyCurve(progress, curve);

        ensurePermutation(outgoing.width(), outgoing.height(), mbSize);
        const int total     = static_cast<int>(m_perm.size());
        const int threshold = static_cast<int>(t * total + 0.5);

        QImage out = outgoing.copy();
        const int cols = (outgoing.width()  + mbSize - 1) / mbSize;
        const int W    = outgoing.width();
        const int H    = outgoing.height();

        // The first `threshold` entries in m_perm get their pixels from
        // `incoming`; remaining entries stay as `outgoing` (no-op since we
        // started from a copy of outgoing).
        for (int i = 0; i < threshold; ++i) {
            const int mbIdx = m_perm[i];
            const int mbX   = (mbIdx % cols) * mbSize;
            const int mbY   = (mbIdx / cols) * mbSize;
            const int w     = std::min(mbSize, W - mbX);
            const int h     = std::min(mbSize, H - mbY);
            if (w <= 0 || h <= 0) continue;

            for (int row = 0; row < h; ++row) {
                const uchar* src = incoming.constScanLine(mbY + row) + mbX * 4;
                uchar*       dst = out.scanLine(mbY + row)          + mbX * 4;
                std::memcpy(dst, src, static_cast<size_t>(w) * 4);
            }
        }
        return out;
    }

private:
    void ensurePermutation(int frameW, int frameH, int mbSize)
    {
        if (m_permW == frameW && m_permH == frameH
            && m_permMBSize == mbSize && !m_perm.empty()) return;
        const int cols = (frameW + mbSize - 1) / mbSize;
        const int rows = (frameH + mbSize - 1) / mbSize;
        const int total = cols * rows;
        m_perm.resize(total);
        for (int i = 0; i < total; ++i) m_perm[i] = i;
        // Fisher-Yates shuffle using our seeded RNG.
        for (int i = total - 1; i > 0; --i) {
            const int j = static_cast<int>(m_rng.bounded(i + 1));
            std::swap(m_perm[i], m_perm[j]);
        }
        m_permW      = frameW;
        m_permH      = frameH;
        m_permMBSize = mbSize;
    }

    QRandomGenerator m_rng;
    std::vector<int> m_perm;
    int m_permW     { 0 };
    int m_permH     { 0 };
    int m_permMBSize{ 0 };
};

// =============================================================================
// Factory + easing + registry
// =============================================================================

std::unique_ptr<Transition> Transition::create(const QString& typeId)
{
    if (typeId == "hard_cut")  return std::make_unique<HardCutTransition>();
    if (typeId == "crossfade") return std::make_unique<CrossfadeTransition>();
    if (typeId == "mb_random") return std::make_unique<MBRandomTransition>();
    return nullptr;
}

std::vector<std::pair<QString, QString>> Transition::availableTypes()
{
    return {
        { "hard_cut",  "Hard Cut"  },
        { "crossfade", "Crossfade" },
        { "mb_random", "MB Random" },
    };
}

double Transition::applyCurve(double t, const QString& curve)
{
    t = std::clamp(t, 0.0, 1.0);
    if (curve == "ease_in")  return t * t;
    if (curve == "ease_out") return 1.0 - (1.0 - t) * (1.0 - t);
    if (curve == "smooth")   return t * t * (3.0 - 2.0 * t); // smoothstep
    return t;   // "linear" / unknown → identity
}

} // namespace sequencer

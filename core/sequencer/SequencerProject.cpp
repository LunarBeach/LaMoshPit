#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/EditCommand.h"
#include "core/sequencer/ClipEffects.h"
#include "core/project/Project.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QWriteLocker>

#include <algorithm>
#include <cassert>

// All public mutators follow this pattern:
//   { QWriteLocker lk(&m_stateLock); ...mutate...; }
//   emit projectChanged();   // after unlock — slots may re-enter reads
// Compositor threads take a QReadLocker externally across their full read
// phase.  Read methods on this class don't self-lock (the GUI-thread owner
// doesn't need to, and cross-thread readers take the lock explicitly via
// stateLock()).

namespace sequencer {

SequencerProject::SequencerProject(QObject* parent)
    : QObject(parent)
{
    // Start empty — Phase 0 ships with no tracks.  The UI (Phase 2) will
    // insert the first track via an AddTrackCmd on user request.
}

SequencerProject::~SequencerProject() = default;

const SequencerTrack& SequencerProject::track(int index) const {
    assert(index >= 0 && index < m_tracks.size());
    return m_tracks[index];
}

SequencerTrack& SequencerProject::trackMutable(int index) {
    assert(index >= 0 && index < m_tracks.size());
    return m_tracks[index];
}

void SequencerProject::setActiveTrackIndex(int idx) {
    {
        QWriteLocker lk(&m_stateLock);
        if (idx < 0 || idx >= m_tracks.size()) return;
        if (idx == m_activeTrackIndex) return;
        m_activeTrackIndex = idx;
    }
    emit activeTrackChanged(idx);
}

void SequencerProject::setLoopInTicks(Tick t) {
    {
        QWriteLocker lk(&m_stateLock);
        if (t < 0) t = 0;
        if (t == m_loopInTicks) return;
        m_loopInTicks = t;
    }
    emit loopChanged();
}

void SequencerProject::setLoopOutTicks(Tick t) {
    {
        QWriteLocker lk(&m_stateLock);
        if (t < 0) t = 0;
        if (t == m_loopOutTicks) return;
        m_loopOutTicks = t;
    }
    emit loopChanged();
}

void SequencerProject::setLoopEnabled(bool on) {
    {
        QWriteLocker lk(&m_stateLock);
        if (on == m_loopEnabled) return;
        m_loopEnabled = on;
    }
    emit loopChanged();
}

void SequencerProject::setOutputFrameRate(AVRational r) {
    {
        QWriteLocker lk(&m_stateLock);
        if (r.num <= 0 || r.den <= 0) return;
        if (r.num == m_outputFrameRate.num && r.den == m_outputFrameRate.den) return;
        m_outputFrameRate = r;
    }
    emit outputFrameRateChanged();
}

Tick SequencerProject::totalDurationTicks() const {
    Tick longest = 0;
    for (const auto& tr : m_tracks) {
        longest = std::max(longest, tr.totalDurationTicks());
    }
    return longest;
}

bool SequencerProject::executeCommand(std::unique_ptr<EditCommand> cmd) {
    if (!cmd) return false;
    bool ok = false;
    {
        QWriteLocker lk(&m_stateLock);
        if (!cmd->redo(*this)) return false;
        ok = true;
        m_undo.push_back(std::move(cmd));
        m_redo.clear();
        // Drop oldest when the stack exceeds the configured cap.  vector's
        // front erase is O(n) but the cap is small (≤ 500) and edits are
        // infrequent compared to rendering / playback cost, so the overhead
        // is negligible versus switching to deque here.
        if (m_maxUndoSteps > 0 && int(m_undo.size()) > m_maxUndoSteps) {
            m_undo.erase(m_undo.begin(),
                         m_undo.begin() + (m_undo.size() - m_maxUndoSteps));
        }
    }
    emit projectChanged();
    return ok;
}

void SequencerProject::undo() {
    {
        QWriteLocker lk(&m_stateLock);
        if (m_undo.empty()) return;
        auto cmd = std::move(m_undo.back());
        m_undo.pop_back();
        cmd->undo(*this);
        m_redo.push_back(std::move(cmd));
    }
    emit projectChanged();
}

void SequencerProject::redo() {
    {
        QWriteLocker lk(&m_stateLock);
        if (m_redo.empty()) return;
        auto cmd = std::move(m_redo.back());
        m_redo.pop_back();
        cmd->redo(*this);
        m_undo.push_back(std::move(cmd));
    }
    emit projectChanged();
}

void SequencerProject::setMaxUndoSteps(int n) {
    if (n < 1) n = 1;
    if (n == m_maxUndoSteps) return;
    m_maxUndoSteps = n;
    // Trim immediately so a shrink takes effect without waiting for the
    // next executeCommand.
    if (int(m_undo.size()) > m_maxUndoSteps) {
        m_undo.erase(m_undo.begin(),
                     m_undo.begin() + (m_undo.size() - m_maxUndoSteps));
    }
}

// =============================================================================
// Serialization
// =============================================================================

namespace {

QJsonObject rationalToJson(AVRational r) {
    QJsonObject o;
    o["num"] = r.num;
    o["den"] = r.den;
    return o;
}

AVRational rationalFromJson(const QJsonValue& v, AVRational fallback) {
    if (!v.isObject()) return fallback;
    const QJsonObject o = v.toObject();
    AVRational r;
    r.num = o.value("num").toInt(fallback.num);
    r.den = o.value("den").toInt(fallback.den);
    if (r.num == 0 || r.den == 0) return fallback;
    return r;
}

QJsonObject clipToJson(const SequencerClip& c, const ::Project& proj) {
    QJsonObject o;
    o["sourcePath"]          = proj.compressToTokens(c.sourcePath);
    o["sourceTimeBase"]      = rationalToJson(c.sourceTimeBase);
    o["sourceFrameRate"]     = rationalToJson(c.sourceFrameRate);
    o["sourceDurationTicks"] = qint64(c.sourceDurationTicks);
    o["sourceInTicks"]       = qint64(c.sourceInTicks);
    o["sourceOutTicks"]      = qint64(c.sourceOutTicks);
    o["timelineStartTicks"]  = qint64(c.timelineStartTicks);
    o["speed"]               = rationalToJson(c.speed);
    // Layer compositor fields — default-valued clips (opacity=1, blend=Normal,
    // no fades) read identically from projects saved before this block
    // existed because clipFromJson falls back to the same defaults.
    o["opacity"]             = double(c.opacity);
    o["blendMode"]           = int(c.blendMode);
    o["fadeInTicks"]         = qint64(c.fadeInTicks);
    o["fadeOutTicks"]        = qint64(c.fadeOutTicks);
    // Effects serialised as an array of stable string ids — forward-
    // compatible across reorders / additions of the ClipEffect enum, and
    // unknown ids on load are silently dropped (see clipFromJson).
    if (!c.effects.isEmpty()) {
        QJsonArray fx;
        for (ClipEffect e : c.effects)
            fx.append(clipEffectId(e));
        o["effects"] = fx;
    }
    return o;
}

SequencerClip clipFromJson(const QJsonObject& o, const ::Project& proj) {
    SequencerClip c;
    c.sourcePath          = proj.expandTokens(o.value("sourcePath").toString());
    c.sourceTimeBase      = rationalFromJson(o.value("sourceTimeBase"),   { 1, 90000 });
    c.sourceFrameRate     = rationalFromJson(o.value("sourceFrameRate"),  { 30, 1 });
    c.sourceDurationTicks = Tick(o.value("sourceDurationTicks").toVariant().toLongLong());
    c.sourceInTicks       = Tick(o.value("sourceInTicks").toVariant().toLongLong());
    c.sourceOutTicks      = Tick(o.value("sourceOutTicks").toVariant().toLongLong());
    c.timelineStartTicks  = Tick(o.value("timelineStartTicks").toVariant().toLongLong());
    c.speed               = rationalFromJson(o.value("speed"),            { 1, 1 });
    c.opacity             = float(o.value("opacity").toDouble(1.0));
    c.blendMode           = BlendMode(o.value("blendMode").toInt(int(BlendMode::Normal)));
    c.fadeInTicks         = Tick(o.value("fadeInTicks").toVariant().toLongLong());
    c.fadeOutTicks        = Tick(o.value("fadeOutTicks").toVariant().toLongLong());
    const QJsonArray fx = o.value("effects").toArray();
    for (const QJsonValue& v : fx) {
        const auto maybe = clipEffectFromId(v.toString());
        if (maybe) c.effects.append(*maybe);
    }
    return c;
}

} // namespace

QJsonObject SequencerProject::toJson(const ::Project& proj) const {
    QJsonObject root;
    root["version"]          = 1;
    root["outputFrameRate"]  = rationalToJson(m_outputFrameRate);
    root["loopInTicks"]      = qint64(m_loopInTicks);
    root["loopOutTicks"]     = qint64(m_loopOutTicks);
    root["loopEnabled"]      = m_loopEnabled;
    root["activeTrackIndex"] = m_activeTrackIndex;

    QJsonArray tracks;
    for (const SequencerTrack& tr : m_tracks) {
        QJsonObject tj;
        tj["name"]    = tr.name;
        tj["enabled"] = tr.enabled;

        QJsonArray clips;
        for (const SequencerClip& c : tr.clips)
            clips.append(clipToJson(c, proj));
        tj["clips"] = clips;

        tracks.append(tj);
    }
    root["tracks"] = tracks;
    return root;
}

bool SequencerProject::fromJson(const QJsonObject& obj, const ::Project& proj) {
    if (obj.isEmpty()) return false;

    int activeIdx = 0;
    {
        QWriteLocker lk(&m_stateLock);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();

        m_outputFrameRate  = rationalFromJson(obj.value("outputFrameRate"), { 30, 1 });
        m_loopInTicks      = Tick(obj.value("loopInTicks").toVariant().toLongLong());
        m_loopOutTicks     = Tick(obj.value("loopOutTicks").toVariant().toLongLong());
        m_loopEnabled      = obj.value("loopEnabled").toBool(false);
        m_activeTrackIndex = obj.value("activeTrackIndex").toInt(0);

        const QJsonArray tracks = obj.value("tracks").toArray();
        for (const QJsonValue& tv : tracks) {
            if (!tv.isObject()) continue;
            const QJsonObject tj = tv.toObject();
            SequencerTrack tr;
            tr.name    = tj.value("name").toString();
            tr.enabled = tj.value("enabled").toBool(true);

            const QJsonArray clips = tj.value("clips").toArray();
            for (const QJsonValue& cv : clips) {
                if (!cv.isObject()) continue;
                tr.clips.append(clipFromJson(cv.toObject(), proj));
            }
            m_tracks.append(tr);
        }

        if (m_activeTrackIndex >= m_tracks.size())
            m_activeTrackIndex = m_tracks.isEmpty() ? 0 : m_tracks.size() - 1;
        activeIdx = m_activeTrackIndex;
    }

    emit projectChanged();
    emit activeTrackChanged(activeIdx);
    emit loopChanged();
    emit outputFrameRateChanged();
    return true;
}

void SequencerProject::clear() {
    {
        QWriteLocker lk(&m_stateLock);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();
        m_activeTrackIndex = 0;
        m_loopInTicks      = 0;
        m_loopOutTicks     = 0;
        m_loopEnabled      = false;
        m_outputFrameRate  = { 30, 1 };
    }
    emit projectChanged();
    emit activeTrackChanged(0);
    emit loopChanged();
    emit outputFrameRateChanged();
}

} // namespace sequencer

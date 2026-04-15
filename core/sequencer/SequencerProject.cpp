#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/EditCommand.h"

#include <algorithm>
#include <cassert>

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
    if (idx < 0 || idx >= m_tracks.size()) return;
    if (idx == m_activeTrackIndex) return;
    m_activeTrackIndex = idx;
    emit activeTrackChanged(idx);
}

void SequencerProject::setLoopInTicks(Tick t) {
    if (t < 0) t = 0;
    if (t == m_loopInTicks) return;
    m_loopInTicks = t;
    emit loopChanged();
}

void SequencerProject::setLoopOutTicks(Tick t) {
    if (t < 0) t = 0;
    if (t == m_loopOutTicks) return;
    m_loopOutTicks = t;
    emit loopChanged();
}

void SequencerProject::setLoopEnabled(bool on) {
    if (on == m_loopEnabled) return;
    m_loopEnabled = on;
    emit loopChanged();
}

void SequencerProject::setOutputFrameRate(AVRational r) {
    if (r.num <= 0 || r.den <= 0) return;
    if (r.num == m_outputFrameRate.num && r.den == m_outputFrameRate.den) return;
    m_outputFrameRate = r;
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
    if (!cmd->redo(*this)) return false;
    m_undo.push_back(std::move(cmd));
    m_redo.clear();
    emit projectChanged();
    return true;
}

void SequencerProject::undo() {
    if (m_undo.empty()) return;
    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    cmd->undo(*this);
    m_redo.push_back(std::move(cmd));
    emit projectChanged();
}

void SequencerProject::redo() {
    if (m_redo.empty()) return;
    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    cmd->redo(*this);
    m_undo.push_back(std::move(cmd));
    emit projectChanged();
}

} // namespace sequencer

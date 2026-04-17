#include "gui/undo/EditorCommands.h"

#include "gui/MainWindow.h"
#include "gui/widgets/MacroblockWidget.h"
#include "gui/widgets/GlobalParamsWidget.h"

namespace undo {

// ============================================================================
// ClipSwitchCommand
// ============================================================================

ClipSwitchCommand::ClipSwitchCommand(MainWindow* mw,
                                     const QString& beforePath,
                                     const QString& afterPath)
    : m_mw(mw)
    , m_beforePath(beforePath)
    , m_afterPath(afterPath)
{}

void ClipSwitchCommand::redo()
{
    if (m_mw) m_mw->switchActiveClipDirect(m_afterPath);
}

void ClipSwitchCommand::undo()
{
    if (m_mw) m_mw->switchActiveClipDirect(m_beforePath);
}

// ============================================================================
// MBEditMapReplaceCommand
// ============================================================================

MBEditMapReplaceCommand::MBEditMapReplaceCommand(MacroblockWidget* mb,
                                                 MBEditMap before,
                                                 MBEditMap after)
    : m_mb(mb)
    , m_before(std::move(before))
    , m_after(std::move(after))
{}

void MBEditMapReplaceCommand::redo()
{
    if (m_mb) m_mb->loadEditMap(m_after);
}

void MBEditMapReplaceCommand::undo()
{
    if (m_mb) m_mb->loadEditMap(m_before);
}

// ============================================================================
// GlobalParamsReplaceCommand
// ============================================================================

GlobalParamsReplaceCommand::GlobalParamsReplaceCommand(GlobalParamsWidget* gp,
                                                       GlobalEncodeParams before,
                                                       GlobalEncodeParams after)
    : m_gp(gp)
    , m_before(before)
    , m_after(after)
{}

void GlobalParamsReplaceCommand::redo()
{
    if (m_gp) m_gp->setParams(m_after);
}

void GlobalParamsReplaceCommand::undo()
{
    if (m_gp) m_gp->setParams(m_before);
}

} // namespace undo

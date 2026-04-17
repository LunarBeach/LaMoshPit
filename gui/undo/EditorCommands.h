#pragma once

// =============================================================================
// EditorCommands — concrete Command subclasses for LaMoshPit's editor UIs.
//
// Three types covered:
//   1. ClipSwitchCommand         — wraps MainWindow::switchActiveClipDirect
//      so every Media Bin click / import completion / project resume
//      becomes an undoable timeline entry.
//   2. MBEditMapReplaceCommand   — whole-map before/after swap of the
//      MacroblockWidget's per-frame FrameMBParams.  Used for every MB
//      editor mutation (knob settle, brush stroke release, Deselect,
//      Paste, Seed, etc.) because MBEditMap is small and swapping the
//      whole thing is simpler than per-field commands.
//   3. GlobalParamsReplaceCommand — same whole-struct before/after swap
//      pattern for the GlobalParamsWidget's GlobalEncodeParams.
//
// All three store copies (MBEditMap is a QMap — cheap to copy in normal
// editing sessions).  Command ownership is unique_ptr via UndoController;
// commands hold raw pointers to widgets (lifetime guaranteed — see
// UndoController class doc).
// =============================================================================

#include "gui/undo/Command.h"
#include "core/model/MBEditData.h"
#include "core/model/GlobalEncodeParams.h"

#include <QString>

class MainWindow;
class MacroblockWidget;
class GlobalParamsWidget;

namespace undo {

// ----------------------------------------------------------------------------
class ClipSwitchCommand : public Command {
public:
    ClipSwitchCommand(MainWindow* mw,
                      const QString& beforePath,
                      const QString& afterPath);

    void redo() override;
    void undo() override;
    QString description() const override { return QStringLiteral("switch clip"); }

private:
    MainWindow* m_mw;
    QString     m_beforePath;
    QString     m_afterPath;
};

// ----------------------------------------------------------------------------
class MBEditMapReplaceCommand : public Command {
public:
    MBEditMapReplaceCommand(MacroblockWidget* mb,
                            MBEditMap before,
                            MBEditMap after);

    void redo() override;
    void undo() override;
    QString description() const override { return QStringLiteral("MB edit"); }

private:
    MacroblockWidget* m_mb;
    MBEditMap         m_before;
    MBEditMap         m_after;
};

// ----------------------------------------------------------------------------
class GlobalParamsReplaceCommand : public Command {
public:
    GlobalParamsReplaceCommand(GlobalParamsWidget* gp,
                               GlobalEncodeParams before,
                               GlobalEncodeParams after);

    void redo() override;
    void undo() override;
    QString description() const override { return QStringLiteral("global params"); }

private:
    GlobalParamsWidget* m_gp;
    GlobalEncodeParams  m_before;
    GlobalEncodeParams  m_after;
};

} // namespace undo

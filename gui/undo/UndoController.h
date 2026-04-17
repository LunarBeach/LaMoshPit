#pragma once

// =============================================================================
// UndoController — single application-wide command stack.
//
// Scope C replaces the previous ad-hoc "Ctrl+Z loads the previous rendered
// version from disk" behaviour with a true unified undo system that walks
// backward through user actions in execution order: parameter-knob changes,
// selection painting, clip switches, and sequencer edits all live on the
// same stack.  A consequence the user specifically asked for: after Ctrl+Z
// undoes a clip switch, continuing to press Ctrl+Z walks back through the
// parameter / selection changes made on the previous clip.
//
// Ownership: MainWindow owns exactly one UndoController.  The controller
// owns its commands (std::unique_ptr).  Commands in turn hold raw pointers
// to widgets; widget lifetime is guaranteed by MainWindow, which tears
// down the controller (via clear()) before any widgets get destroyed.
//
// Max stack size: read at construction from SettingsDialog::maxUndoSteps().
// When the undo stack exceeds this, the OLDEST command is dropped (FIFO at
// the bottom).  The redo stack is bounded by the same number but typically
// stays short because pushing a new command clears it.
// =============================================================================

#include "gui/undo/Command.h"

#include <QObject>
#include <deque>
#include <memory>

namespace undo {

class UndoController : public QObject {
    Q_OBJECT
public:
    explicit UndoController(QObject* parent = nullptr);

    // Take ownership of cmd, call redo() once, push onto the undo stack,
    // and clear the redo stack (new branches invalidate the redo future).
    void execute(std::unique_ptr<Command> cmd);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    // Drop every command on both stacks.  Called on project switch so
    // history from project A doesn't get applied to widgets now showing
    // project B's content (the widget pointers are the same but the
    // captured before/after state belongs to a different project).
    void clear();

    int  maxSteps() const { return m_maxSteps; }
    void setMaxSteps(int n);

public slots:
    void undo();
    void redo();

signals:
    // canUndo / canRedo flipped, or the index moved.  MainWindow uses
    // this to enable/disable the Edit menu's Undo/Redo actions.
    void stateChanged();

    // Fired after every successful execute / undo / redo.  MainWindow
    // uses this to mark the active Project dirty — every user action is
    // a change from disk state until the user saves.
    void commandExecuted();

private:
    void trimOldest();

    std::deque<std::unique_ptr<Command>> m_undo;
    std::deque<std::unique_ptr<Command>> m_redo;
    int                                   m_maxSteps { 50 };
};

} // namespace undo

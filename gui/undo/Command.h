#pragma once

// =============================================================================
// Command — abstract base for the unified application-wide undo/redo system.
//
// Each user action that mutates project-saved state produces one Command
// object.  The command captures whatever "before" / "after" information is
// needed so that redo() and undo() can both execute without any external
// state being threaded through.  Commands live on UndoController's stack
// for the duration of the session (not persisted across app restarts —
// industry norm; persisting a command graph is a much bigger design).
//
// Design notes:
// - Commands are small data holders.  They do NOT capture a pointer to the
//   UndoController and must not push more commands during redo/undo —
//   that would cause re-entry into the controller's stack.
// - Commands hold raw pointers to the widgets they mutate.  Lifetime is
//   guaranteed by MainWindow owning both the widgets and the controller;
//   the controller is cleared before widgets go away.
// - redo() is called once on execute() and again on subsequent redo presses.
//   It must be idempotent with respect to the widget state the command
//   was built from — i.e. always produce the same "after" state regardless
//   of what's live in the widget when it runs.
// =============================================================================

#include <QString>

namespace undo {

class Command {
public:
    virtual ~Command() = default;

    // Apply the change.  Called on execute() and on every redo() press.
    virtual void redo() = 0;

    // Reverse the change.  Called on every undo() press.
    virtual void undo() = 0;

    // Short human-readable label used for menu text and debug logging
    // (e.g. "Switch clip", "MB edit", "Global params").  Not user-facing
    // in the v1 menu (which just says "Undo" / "Redo") but wired up so a
    // later "Undo <last-action>" label is a small change.
    virtual QString description() const { return QStringLiteral("edit"); }
};

} // namespace undo

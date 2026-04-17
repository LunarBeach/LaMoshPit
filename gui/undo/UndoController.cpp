#include "gui/undo/UndoController.h"

namespace undo {

UndoController::UndoController(QObject* parent)
    : QObject(parent)
{}

void UndoController::execute(std::unique_ptr<Command> cmd)
{
    if (!cmd) return;
    cmd->redo();
    m_undo.push_back(std::move(cmd));
    m_redo.clear();
    trimOldest();
    emit commandExecuted();
    emit stateChanged();
}

void UndoController::undo()
{
    if (m_undo.empty()) return;
    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    cmd->undo();
    m_redo.push_back(std::move(cmd));
    emit commandExecuted();
    emit stateChanged();
}

void UndoController::redo()
{
    if (m_redo.empty()) return;
    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    cmd->redo();
    m_undo.push_back(std::move(cmd));
    emit commandExecuted();
    emit stateChanged();
}

void UndoController::clear()
{
    const bool had = !m_undo.empty() || !m_redo.empty();
    m_undo.clear();
    m_redo.clear();
    if (had) emit stateChanged();
}

void UndoController::setMaxSteps(int n)
{
    if (n < 1) n = 1;
    if (n == m_maxSteps) return;
    m_maxSteps = n;
    trimOldest();
    emit stateChanged();
}

void UndoController::trimOldest()
{
    while (int(m_undo.size()) > m_maxSteps) m_undo.pop_front();
    while (int(m_redo.size()) > m_maxSteps) m_redo.pop_front();
}

} // namespace undo

#include "tools/command_stack.hpp"

#include <utility>

namespace saycheese::tools {

void CommandStack::PushAndExecute(std::unique_ptr<ICommand> cmd) {
    if (!cmd) {
        return;
    }
    cmd->Execute();
    undoStack_.push_back(std::move(cmd));
    redoStack_.clear();
}

bool CommandStack::Undo() {
    if (undoStack_.empty()) {
        return false;
    }
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->Undo();
    redoStack_.push_back(std::move(cmd));
    return true;
}

bool CommandStack::Redo() {
    if (redoStack_.empty()) {
        return false;
    }
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->Execute();
    undoStack_.push_back(std::move(cmd));
    return true;
}

void CommandStack::Clear() noexcept {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace saycheese::tools

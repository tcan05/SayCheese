#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace saycheese::tools {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

class CommandStack {
public:
    void PushAndExecute(std::unique_ptr<ICommand> cmd);
    bool Undo();
    bool Redo();

    bool CanUndo() const noexcept { return !undoStack_.empty(); }
    bool CanRedo() const noexcept { return !redoStack_.empty(); }
    size_t UndoCount() const noexcept { return undoStack_.size(); }
    size_t RedoCount() const noexcept { return redoStack_.size(); }

    void Clear() noexcept;

private:
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
};

} // namespace saycheese::tools

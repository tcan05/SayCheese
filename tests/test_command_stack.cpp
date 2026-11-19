#include "test_runner.hpp"

#include "tools/command_stack.hpp"

#include <memory>
#include <vector>

using namespace saycheese::tools;

namespace {

class AppendIntCommand final : public ICommand {
public:
    AppendIntCommand(std::vector<int>& target, int value) : target_(target), value_(value) {}
    void Execute() override { target_.push_back(value_); }
    void Undo() override {
        if (!target_.empty()) {
            target_.pop_back();
        }
    }

private:
    std::vector<int>& target_;
    int value_;
};

} // namespace

SC_TEST("CommandStack supports push, undo, redo, branching history truncation, and clear") {
    std::vector<int> state;
    CommandStack stack;

    SC_EXPECT_FALSE(stack.CanUndo());
    SC_EXPECT_FALSE(stack.CanRedo());
    SC_EXPECT_FALSE(stack.Undo());
    SC_EXPECT_FALSE(stack.Redo());

    stack.PushAndExecute(std::make_unique<AppendIntCommand>(state, 10));
    stack.PushAndExecute(std::make_unique<AppendIntCommand>(state, 20));
    stack.PushAndExecute(std::make_unique<AppendIntCommand>(state, 30));

    SC_EXPECT_EQ(state.size(), static_cast<size_t>(3));
    SC_EXPECT_EQ(stack.UndoCount(), static_cast<size_t>(3));
    SC_EXPECT_EQ(stack.RedoCount(), static_cast<size_t>(0));

    SC_EXPECT_TRUE(stack.Undo());
    SC_EXPECT_TRUE(stack.Undo());
    SC_EXPECT_EQ(state.size(), static_cast<size_t>(1));
    SC_EXPECT_EQ(state[0], 10);
    SC_EXPECT_EQ(stack.UndoCount(), static_cast<size_t>(1));
    SC_EXPECT_EQ(stack.RedoCount(), static_cast<size_t>(2));

    SC_EXPECT_TRUE(stack.Redo());
    SC_EXPECT_EQ(state.size(), static_cast<size_t>(2));
    SC_EXPECT_EQ(state[1], 20);
    SC_EXPECT_EQ(stack.RedoCount(), static_cast<size_t>(1));

    // New command clears remaining redo branch.
    stack.PushAndExecute(std::make_unique<AppendIntCommand>(state, 99));
    SC_EXPECT_FALSE(stack.CanRedo());
    SC_EXPECT_EQ(stack.RedoCount(), static_cast<size_t>(0));
    SC_EXPECT_EQ(state.size(), static_cast<size_t>(3));
    SC_EXPECT_EQ(state[2], 99);

    stack.Clear();
    SC_EXPECT_FALSE(stack.CanUndo());
    SC_EXPECT_FALSE(stack.CanRedo());
    SC_EXPECT_EQ(stack.UndoCount(), static_cast<size_t>(0));
    SC_EXPECT_EQ(stack.RedoCount(), static_cast<size_t>(0));
}

int main() {
    return saycheese::test::RunAll("command_stack");
}

/// --------------------
/// Adaptive expression JIT
/// --------------------

#ifndef JIT_H
#define JIT_H

#include "node.h"
#include "symboltable.h"
#include "value.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class JitOp {
    PushInt,
    PushFloat,
    LoadVar,
    LoadArray,
    PushArray,
    PopArray,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    And,
    Or,
    Negate,
    Not,
};

struct JitInstruction {
    JitOp op;
    int64_t int_value{0};
    double float_value{0.0};
    std::string name;
};

class JitProgram {
public:
    explicit JitProgram(std::vector<JitInstruction> _instructions)
        : instructions(std::move(_instructions)) {}

    std::optional<std::shared_ptr<Value>> execute(SymbolTable &symbols) const;

private:
    std::vector<JitInstruction> instructions;
};

class ExpressionJit {
public:
    static std::optional<JitProgram> compile(const std::shared_ptr<Node>& node);
};

#endif

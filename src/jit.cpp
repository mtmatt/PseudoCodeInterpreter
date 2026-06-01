/// --------------------
/// Adaptive expression JIT
/// --------------------

#include "jit.h"
#include "color.h"
#include "token.h"
#include <cmath>

namespace {

struct JitNumber {
    bool is_float{false};
    int64_t int_value{0};
    double float_value{0.0};

    static JitNumber from_int(int64_t value) {
        return JitNumber{false, value, static_cast<double>(value)};
    }

    static JitNumber from_float(double value) {
        return JitNumber{true, static_cast<int64_t>(value), value};
    }

    double as_double() const {
        return is_float ? float_value : static_cast<double>(int_value);
    }

    int64_t as_int() const {
        return is_float ? static_cast<int64_t>(float_value) : int_value;
    }
};

bool push_binary_op(const std::string& token_type,
                    const std::string& token_value,
                    std::vector<JitInstruction>& instructions) {
    if (token_type == TOKEN_ADD) instructions.push_back({JitOp::Add});
    else if (token_type == TOKEN_SUB) instructions.push_back({JitOp::Sub});
    else if (token_type == TOKEN_MUL) instructions.push_back({JitOp::Mul});
    else if (token_type == TOKEN_DIV) instructions.push_back({JitOp::Div});
    else if (token_type == TOKEN_MOD) instructions.push_back({JitOp::Mod});
    else if (token_type == TOKEN_POW) instructions.push_back({JitOp::Pow});
    else if (token_type == TOKEN_EQUAL) instructions.push_back({JitOp::Equal});
    else if (token_type == TOKEN_NEQ) instructions.push_back({JitOp::NotEqual});
    else if (token_type == TOKEN_LESS) instructions.push_back({JitOp::Less});
    else if (token_type == TOKEN_GREATER) instructions.push_back({JitOp::Greater});
    else if (token_type == TOKEN_LEQ) instructions.push_back({JitOp::LessEqual});
    else if (token_type == TOKEN_GEQ) instructions.push_back({JitOp::GreaterEqual});
    else if (token_type == TOKEN_KEYWORD && token_value == "and") instructions.push_back({JitOp::And});
    else if (token_type == TOKEN_KEYWORD && token_value == "or") instructions.push_back({JitOp::Or});
    else return false;
    return true;
}

bool push_unary_op(const std::string& token_type,
                   const std::string& token_value,
                   std::vector<JitInstruction>& instructions) {
    if (token_type == TOKEN_ADD) return true;
    if (token_type == TOKEN_SUB) instructions.push_back({JitOp::Negate});
    else if (token_type == TOKEN_KEYWORD && token_value == "not") instructions.push_back({JitOp::Not});
    else return false;
    return true;
}

bool compile_node(const std::shared_ptr<Node>& node,
                  std::vector<JitInstruction>& instructions) {
    if (!node) return false;

    const std::string node_type = node->get_type();
    if (node_type == NODE_VALUE) {
        std::shared_ptr<Token> token = node->get_tok();
        if (token->get_type() == TOKEN_INT) {
            instructions.push_back({JitOp::PushInt, std::stoll(token->get_value())});
            return true;
        }
        if (token->get_type() == TOKEN_FLOAT) {
            JitInstruction instruction{JitOp::PushFloat};
            instruction.float_value = std::stod(token->get_value());
            instructions.push_back(instruction);
            return true;
        }
        return false;
    }

    if (node_type == NODE_VARACCESS) {
        JitInstruction instruction{JitOp::LoadVar};
        instruction.name = node->get_name();
        instructions.push_back(instruction);
        return true;
    }

    if (node_type == NODE_BINOP) {
        NodeList child = node->get_child();
        if (child.size() != 2) return false;
        if (!compile_node(child[0], instructions)) return false;
        if (!compile_node(child[1], instructions)) return false;
        std::shared_ptr<Token> token = node->get_tok();
        return push_binary_op(token->get_type(), token->get_value(), instructions);
    }

    if (node_type == NODE_UNARYOP) {
        NodeList child = node->get_child();
        if (child.size() != 1) return false;
        if (!compile_node(child[0], instructions)) return false;
        std::shared_ptr<Token> token = node->get_tok();
        return push_unary_op(token->get_type(), token->get_value(), instructions);
    }

    return false;
}

std::shared_ptr<Value> to_value(const JitNumber& number) {
    if (number.is_float) {
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, number.float_value);
    }
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, number.int_value);
}

std::shared_ptr<Value> runtime_error(const std::string& message) {
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + message + RESET);
}

} // namespace

std::optional<JitProgram> ExpressionJit::compile(const std::shared_ptr<Node>& node) {
    std::vector<JitInstruction> instructions;
    if (!compile_node(node, instructions)) {
        return std::nullopt;
    }
    return JitProgram(std::move(instructions));
}

std::optional<std::shared_ptr<Value>> JitProgram::execute(SymbolTable &symbols) const {
    std::vector<JitNumber> stack;
    stack.reserve(instructions.size());

    auto pop = [&stack]() -> std::optional<JitNumber> {
        if (stack.empty()) return std::nullopt;
        JitNumber value = stack.back();
        stack.pop_back();
        return value;
    };

    for (const JitInstruction& instruction : instructions) {
        switch (instruction.op) {
        case JitOp::PushInt:
            stack.push_back(JitNumber::from_int(instruction.int_value));
            break;
        case JitOp::PushFloat:
            stack.push_back(JitNumber::from_float(instruction.float_value));
            break;
        case JitOp::LoadVar: {
            std::shared_ptr<Value> value = symbols.get(instruction.name);
            if (value->get_type() == VALUE_ERROR) return value;
            if (value->get_type() == VALUE_INT) stack.push_back(JitNumber::from_int(value->as_int()));
            else if (value->get_type() == VALUE_FLOAT) stack.push_back(JitNumber::from_float(value->as_double()));
            else return std::nullopt;
            break;
        }
        case JitOp::Add:
        case JitOp::Sub:
        case JitOp::Mul:
        case JitOp::Div:
        case JitOp::Mod:
        case JitOp::Pow:
        case JitOp::Equal:
        case JitOp::NotEqual:
        case JitOp::Less:
        case JitOp::Greater:
        case JitOp::LessEqual:
        case JitOp::GreaterEqual:
        case JitOp::And:
        case JitOp::Or: {
            std::optional<JitNumber> rhs = pop();
            std::optional<JitNumber> lhs = pop();
            if (!lhs || !rhs) return std::nullopt;
            const bool use_float = lhs->is_float || rhs->is_float;
            switch (instruction.op) {
            case JitOp::Add:
                stack.push_back(use_float
                    ? JitNumber::from_float(lhs->as_double() + rhs->as_double())
                    : JitNumber::from_int(lhs->int_value + rhs->int_value));
                break;
            case JitOp::Sub:
                stack.push_back(use_float
                    ? JitNumber::from_float(lhs->as_double() - rhs->as_double())
                    : JitNumber::from_int(lhs->int_value - rhs->int_value));
                break;
            case JitOp::Mul:
                stack.push_back(use_float
                    ? JitNumber::from_float(lhs->as_double() * rhs->as_double())
                    : JitNumber::from_int(lhs->int_value * rhs->int_value));
                break;
            case JitOp::Div:
                if (rhs->as_double() == 0.0) return runtime_error("Runtime ERROR: DIV by 0\n");
                stack.push_back(use_float
                    ? JitNumber::from_float(lhs->as_double() / rhs->as_double())
                    : JitNumber::from_int(lhs->int_value / rhs->int_value));
                break;
            case JitOp::Mod:
                if (lhs->is_float || rhs->is_float) return std::nullopt;
                stack.push_back(JitNumber::from_int(lhs->int_value % rhs->int_value));
                break;
            case JitOp::Pow:
                if (lhs->as_double() == 0.0 && rhs->as_double() == 0.0) {
                    return runtime_error("Runtime ERROR: 0 to the 0\n");
                }
                stack.push_back(use_float
                    ? JitNumber::from_float(std::pow(lhs->as_double(), rhs->as_double()))
                    : JitNumber::from_int(static_cast<int64_t>(std::pow(lhs->int_value, rhs->int_value))));
                break;
            case JitOp::Equal:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() == rhs->as_double()
                    : lhs->int_value == rhs->int_value));
                break;
            case JitOp::NotEqual:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() != rhs->as_double()
                    : lhs->int_value != rhs->int_value));
                break;
            case JitOp::Less:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() < rhs->as_double()
                    : lhs->int_value < rhs->int_value));
                break;
            case JitOp::Greater:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() > rhs->as_double()
                    : lhs->int_value > rhs->int_value));
                break;
            case JitOp::LessEqual:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() <= rhs->as_double()
                    : lhs->int_value <= rhs->int_value));
                break;
            case JitOp::GreaterEqual:
                stack.push_back(JitNumber::from_int(use_float
                    ? lhs->as_double() >= rhs->as_double()
                    : lhs->int_value >= rhs->int_value));
                break;
            case JitOp::And:
                stack.push_back(JitNumber::from_int(lhs->as_double() != 0.0 && rhs->as_double() != 0.0));
                break;
            case JitOp::Or:
                stack.push_back(JitNumber::from_int(lhs->as_double() != 0.0 || rhs->as_double() != 0.0));
                break;
            default:
                return std::nullopt;
            }
            break;
        }
        case JitOp::Negate: {
            std::optional<JitNumber> value = pop();
            if (!value) return std::nullopt;
            stack.push_back(value->is_float
                ? JitNumber::from_float(-value->float_value)
                : JitNumber::from_int(-value->int_value));
            break;
        }
        case JitOp::Not: {
            std::optional<JitNumber> value = pop();
            if (!value) return std::nullopt;
            if (value->is_float) {
                stack.push_back(JitNumber::from_float(value->as_double() == 0.0));
            } else {
                stack.push_back(JitNumber::from_int(value->as_int() == 0));
            }
            break;
        }
        }
    }

    if (stack.size() != 1) return std::nullopt;
    return to_value(stack.back());
}

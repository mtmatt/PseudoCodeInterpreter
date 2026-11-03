#include "complexity.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace {

using NodePtr = std::shared_ptr<Node>;

struct Cost {
    int power{0};
    int logs{0};
    std::set<std::string> reasons;
    std::set<std::string> notes;

    static Cost unknown(const std::string& reason) {
        Cost result;
        result.reasons.insert(reason);
        return result;
    }

    std::string text() const {
        if (!reasons.empty()) return "unknown";
        std::string result;
        if (power == 1) result = "n";
        if (power > 1) result = "n^" + std::to_string(power);
        if (logs > 0) {
            if (!result.empty()) result += " ";
            result += logs == 1 ? "log n" : "(log n)^" + std::to_string(logs);
        }
        return "O(" + (result.empty() ? "1" : result) + ")";
    }
};

Cost combine(Cost left, const Cost& right, bool multiply = false) {
    if (multiply) {
        left.power += right.power;
        left.logs += right.logs;
    } else if (std::make_pair(right.power, right.logs) > std::make_pair(left.power, left.logs)) {
        left.power = right.power;
        left.logs = right.logs;
    }
    left.reasons.insert(right.reasons.begin(), right.reasons.end());
    left.notes.insert(right.notes.begin(), right.notes.end());
    return left;
}

// Magnitude of a scalar, or length of a known array. Missing constants still
// carry a polynomial upper bound, allowing n*n and simple aliases as bounds.
struct Bound {
    bool known{false};
    int power{0};
    std::optional<long double> constant;
    bool array{false};

    static Bound input() { return {true, 1, std::nullopt, false}; }
    static Bound number(long double value) { return {true, 0, value, false}; }
};

using Environment = std::map<std::string, Bound>;

void invalidate_arrays(Environment& env) {
    // Array assignment and argument passing share storage. Without alias
    // analysis, a mutation may change the length through any array binding.
    for (auto& entry : env) {
        if (entry.second.array) entry.second = {false, 0, std::nullopt, true};
    }
}

NodeList children(const NodePtr& node) {
    if (!node) return {};
    if (node->get_type() == NODE_IF) {
        auto branch = std::static_pointer_cast<IfNode>(node);
        NodeList result{branch->get_condition()};
        result.insert(result.end(), branch->get_expr().begin(), branch->get_expr().end());
        result.insert(result.end(), branch->get_else().begin(), branch->get_else().end());
        return result;
    }
    if (node->get_type() == NODE_ALGOCALL) {
        auto call = std::static_pointer_cast<AlgorithmCallNode>(node);
        NodeList result{call->get_call()};
        result.insert(result.end(), call->get_args().begin(), call->get_args().end());
        return result;
    }
    return node->get_child();
}

std::optional<long double> number(const NodePtr& node) {
    if (!node) return std::nullopt;
    if (node->get_type() == NODE_VALUE && node->get_tok()->get_type() == TOKEN_INT) {
        return std::stold(node->get_tok()->get_value());
    }
    if (node->get_type() == NODE_UNARYOP) {
        auto operand = number(node->get_child()[0]);
        if (!operand) return std::nullopt;
        if (node->get_tok()->get_type() == TOKEN_SUB) return -*operand;
        if (node->get_tok()->get_type() == TOKEN_ADD) return operand;
    }
    return std::nullopt;
}

bool is_variable(const NodePtr& node, const std::string& name) {
    return node && node->get_type() == NODE_VARACCESS && node->get_name() == name;
}

// A scalar counter or bound must not change elsewhere in a loop. Array writes
// and mutating methods also invalidate lengths used by later loops.
void writes(const NodePtr& node, std::map<std::string, int>& names, bool& continues) {
    if (!node) return;
    if (node->get_type() == NODE_ALGODEF || node->get_type() == NODE_STRUCTDEF) {
        ++names[node->get_name()];
        return;
    }
    if (node->get_type() == NODE_CONTINUE) continues = true;
    if (node->get_type() == NODE_VARASSIGN) ++names[node->get_name()];
    if (node->get_type() == NODE_ARRASSIGN) {
        NodePtr target = node->get_child()[0];
        while (target &&
               (target->get_type() == NODE_ARRACCESS || target->get_type() == NODE_MEMACCESS)) {
            target = target->get_child()[0];
        }
        if (target && target->get_type() == NODE_VARACCESS) ++names[target->get_name()];
    }
    if (node->get_type() == NODE_ALGOCALL) {
        auto call = std::static_pointer_cast<AlgorithmCallNode>(node);
        auto callee = call->get_call();
        if (callee->get_type() == NODE_MEMACCESS) {
            auto parts = callee->get_child();
            if (parts[0]->get_type() == NODE_VARACCESS && parts[1]->get_name() != "size") {
                ++names[parts[0]->get_name()];
            }
        }
    }
    for (const auto& child : children(node)) writes(child, names, continues);
}

bool reads_any(const NodePtr& node, const std::map<std::string, int>& names) {
    if (!node) return false;
    if (node->get_type() == NODE_VARACCESS && names.count(node->get_name())) return true;
    for (const auto& child : children(node)) {
        if (reads_any(child, names)) return true;
    }
    return false;
}

bool scalar_expression(const NodePtr& node) {
    if (!node) return false;
    if (node->get_type() == NODE_VALUE || node->get_type() == NODE_VARACCESS) return true;
    if (node->get_type() != NODE_BINOP && node->get_type() != NODE_UNARYOP) return false;
    for (const auto& child : children(node)) {
        if (!scalar_expression(child)) return false;
    }
    return true;
}

bool may_mutate_arrays(const NodePtr& node) {
    if (!node) return false;
    if (node->get_type() == NODE_ALGODEF || node->get_type() == NODE_STRUCTDEF) return false;
    if (node->get_type() == NODE_ALGOCALL || node->get_type() == NODE_ARRASSIGN) return true;
    for (const auto& child : children(node)) {
        if (may_mutate_arrays(child)) return true;
    }
    return false;
}

void invalidate(Environment& env, const NodeList& body) {
    std::map<std::string, int> names;
    bool continues = false;
    bool mutates_arrays = false;
    for (const auto& node : body) {
        writes(node, names, continues);
        mutates_arrays = mutates_arrays || may_mutate_arrays(node);
    }
    if (!names.empty() || mutates_arrays) invalidate_arrays(env);
    for (const auto& name : names) {
        bool array = env.count(name.first) && env.at(name.first).array;
        env[name.first] = {};
        env[name.first].array = array;
    }
}

Bound value_bound(const NodePtr& node, const Environment& env) {
    if (!node) return {};
    auto literal = number(node);
    if (literal) return Bound::number(*literal);
    if (node->get_type() == NODE_VARACCESS) {
        auto found = env.find(node->get_name());
        return found == env.end() ? Bound::input() : found->second;
    }
    if (node->get_type() == NODE_ARRAY) {
        Bound result = Bound::number(node->get_child().size());
        result.array = true;
        return result;
    }
    if (node->get_type() == NODE_ALGOCALL) {
        auto call = std::static_pointer_cast<AlgorithmCallNode>(node);
        auto callee = call->get_call();
        if (callee->get_type() == NODE_MEMACCESS && call->get_args().empty()) {
            auto parts = callee->get_child();
            Bound receiver = value_bound(parts[0], env);
            if (parts[1]->get_name() == "size" && receiver.array) {
                receiver.array = false;
                return receiver;
            }
        }
        return {};
    }
    if (node->get_type() != NODE_BINOP) return {};
    auto parts = node->get_child();
    Bound left = value_bound(parts[0], env);
    Bound right = value_bound(parts[1], env);
    if (!left.known || !right.known || left.array || right.array) return {};
    TokenType op = node->get_tok()->get_type();
    Bound result{true, std::max(left.power, right.power), std::nullopt, false};
    if (op == TOKEN_ADD || op == TOKEN_SUB) {
        if (left.constant && right.constant) {
            result.constant =
                *left.constant + (op == TOKEN_ADD ? *right.constant : -*right.constant);
        }
    } else if (op == TOKEN_MUL) {
        result.power = left.power + right.power;
        if (left.constant && right.constant) result.constant = *left.constant * *right.constant;
    } else if (op == TOKEN_DIV && right.constant && *right.constant != 0) {
        result.power = left.power;
        if (left.constant) result.constant = std::trunc(*left.constant / *right.constant);
    } else if (op == TOKEN_POW && right.constant && *right.constant >= 0 && *right.constant <= 16) {
        result.power = left.power * static_cast<int>(*right.constant);
        if (left.constant) result.constant = std::pow(*left.constant, *right.constant);
    } else {
        return {};
    }
    if (result.power > 128 || (result.constant && !std::isfinite(*result.constant))) return {};
    return result;
}

struct Definition {
    std::string label;
    NodePtr node;
};

class Analyzer {
   public:
    explicit Analyzer(const NodeList& ast) {
        for (const auto& node : ast) {
            if (node->get_type() == NODE_ALGODEF) {
                definitions.push_back({node->get_name(), node});
                if (functions.count(node->get_name())) ambiguous.insert(node->get_name());
                functions[node->get_name()] = node;
            } else if (node->get_type() == NODE_STRUCTDEF) {
                for (const auto& method : node->get_child()) {
                    definitions.push_back({node->get_name() + "." + method->get_name(), method});
                }
            }
        }
        bool continues = false;
        std::map<std::string, int> assigned;
        for (const auto& node : ast) {
            if (node->get_type() != NODE_ALGODEF && node->get_type() != NODE_STRUCTDEF) {
                writes(node, assigned, continues);
            }
        }
        for (const auto& name : assigned) {
            if (functions.count(name.first)) ambiguous.insert(name.first);
        }
    }

    std::string report(const NodeList& ast) {
        std::ostringstream out;
        out << "Static time complexity (source-level upper-bound estimates)\n"
            << "n bounds integer input magnitudes and array lengths; scalar arithmetic and "
               "indexing cost O(1).\n"
            << "Sequential costs use the largest term; nested loops multiply; branches use "
               "the larger path.\n"
            << "Unknown means a bound or call could not be established. These are conditional "
               "estimates, not termination proofs.\n";
        Environment env;
        append(out, "Program", block(ast, env));
        for (const auto& definition : definitions) {
            Environment args;
            for (const auto& arg : definition.node->get_toks())
                args[arg->get_value()] = Bound::input();
            active.insert(definition.node->get_id());
            remaining = 100000;
            Cost cost = block(definition.node->get_child(), args);
            active.erase(definition.node->get_id());
            append(out, "Algorithm " + definition.label, cost);
        }
        out << "Model excludes I/O, allocation and string/collection internals, numeric overflow, "
               "and compiler/JIT or memoization effects.\n";
        return out.str();
    }

   private:
    void append(std::ostringstream& out, const std::string& label, const Cost& cost) {
        out << label << ": " << cost.text() << "\n";
        const auto& details = cost.reasons.empty() ? cost.notes : cost.reasons;
        for (const auto& detail : details) out << "  - " << detail << "\n";
    }

    Cost block(const NodeList& nodes, Environment& env) {
        Cost result;
        for (const auto& node : nodes) {
            result = combine(result, statement(node, env));
            if (node->get_type() == NODE_RETURN || node->get_type() == NODE_BREAK ||
                node->get_type() == NODE_CONTINUE) {
                break;
            }
        }
        return result;
    }

    Cost call_cost(const std::shared_ptr<AlgorithmCallNode>& call, Environment& env) {
        Cost result;
        std::vector<Bound> args;
        auto callee = call->get_call();
        TokenList parameters;
        if (callee->get_type() == NODE_VARACCESS && functions.count(callee->get_name()) &&
            !ambiguous.count(callee->get_name()) && !env.count(callee->get_name())) {
            parameters = functions.at(callee->get_name())->get_toks();
        }
        Environment argument_env = env;
        for (size_t i = 0; i < call->get_args().size(); ++i) {
            auto arg = call->get_args()[i];
            args.push_back(value_bound(arg, argument_env));
            result = combine(result, statement(arg, argument_env));
            // Captured array arguments still share storage. A later argument
            // can mutate an earlier one before the callee starts executing.
            if (may_mutate_arrays(arg)) {
                for (auto& bound : args) {
                    if (bound.array) bound = {false, 0, std::nullopt, true};
                }
            }
        }
        // Scalar argument assignments stay in the child scope, but array
        // mutations can affect shared storage in the caller.
        if (!args.empty()) invalidate_arrays(env);
        if (callee->get_type() == NODE_MEMACCESS) {
            auto parts = callee->get_child();
            Bound receiver = value_bound(parts[0], env);
            const std::string method = parts[1]->get_name();
            result = combine(result, statement(parts[0], env));
            if (receiver.array && method == "size" && args.empty()) return result;
            if (receiver.array && (method == "push" || method == "push_back") && args.size() == 1) {
                invalidate_arrays(env);
                result.notes.insert("Array push is treated as amortized O(1).");
                return result;
            }
            invalidate_arrays(env);
            return combine(result, Cost::unknown("Dynamic method call '" + method + "'."));
        }
        if (callee->get_type() != NODE_VARACCESS) {
            return combine(result, Cost::unknown("Indirect algorithm call."));
        }
        const std::string name = callee->get_name();
        if (callee->get_tok()->get_type() == TOKEN_BUILTIN_ALGO && name == "print" &&
            !functions.count(name) && !env.count(name)) {
            result.notes.insert("print I/O and formatting work are excluded.");
            return result;
        }
        auto found = functions.find(name);
        if (found == functions.end() || ambiguous.count(name) || env.count(name)) {
            return combine(result, Cost::unknown("Unresolved or rebound call '" + name + "'."));
        }
        auto function = found->second;
        if (active.count(function->get_id())) {
            return combine(result, Cost::unknown("Recursive call involving '" + name + "'."));
        }
        if (active.size() >= 64)
            return combine(result, Cost::unknown("Call analysis depth limit."));
        if (args.size() != parameters.size()) {
            return combine(result, Cost::unknown("Argument count mismatch for '" + name + "'."));
        }
        Environment local = argument_env;
        for (size_t i = 0; i < args.size(); ++i) local[parameters[i]->get_value()] = args[i];
        active.insert(function->get_id());
        result = combine(result, block(function->get_child(), local));
        active.erase(function->get_id());
        invalidate_arrays(env);
        return result;
    }

    Cost for_cost(const NodePtr& node, Environment& env) {
        auto parts = node->get_child();
        Bound start = value_bound(parts[0]->get_child()[0], env);
        Cost setup = statement(parts[0], env);
        // Match execution order: bind the counter, then evaluate step and end.
        // The end expression may refer to the newly assigned counter.
        Bound step = parts[2] ? value_bound(parts[2], env) : Bound::number(1);
        setup = combine(setup, statement(parts[2], env));
        Bound end = value_bound(parts[1], env);
        setup = combine(setup, statement(parts[1], env));
        NodeList body(parts.begin() + 3, parts.end());
        Cost iterations;
        if (!start.known || !end.known || start.array || end.array || !step.constant ||
            *step.constant == 0) {
            iterations =
                Cost::unknown("for-loop bounds or nonzero constant step are not established.");
        } else {
            iterations.power = std::max(start.power, end.power);
            iterations.notes.insert(iterations.power == 0
                                        ? "for-loop has constant bounds."
                                        : "for-loop uses a polynomial input bound.");
        }
        Environment local = env;
        // Model an arbitrary iteration, not just the first. Values carried
        // from earlier iterations may have grown beyond their initial bound.
        invalidate(local, body);
        local[parts[0]->get_name()] = {start.known && end.known, std::max(start.power, end.power),
                                       std::nullopt, false};
        Cost body_cost = block(body, local);
        invalidate(env, body);
        env[parts[0]->get_name()] = {};
        return combine(setup, combine(iterations, body_cost, true));
    }

    Cost while_cost(const NodePtr& node, Environment& env) {
        auto parts = node->get_child();
        auto condition = parts[0];
        NodeList body(parts.begin() + 1, parts.end());
        Cost iterations = Cost::unknown("Loop termination or iteration bound is not established.");
        if (condition->get_type() == NODE_BINOP) {
            auto comparison = condition->get_child();
            if (comparison[0]->get_type() == NODE_VARACCESS) {
                const std::string counter = comparison[0]->get_name();
                Bound start = value_bound(comparison[0], env);
                Bound end = value_bound(comparison[1], env);
                TokenType relation = condition->get_tok()->get_type();
                if (node->get_type() == NODE_REPEAT) {
                    if (relation == TOKEN_LESS)
                        relation = TOKEN_GEQ;
                    else if (relation == TOKEN_LEQ)
                        relation = TOKEN_GREATER;
                    else if (relation == TOKEN_GREATER)
                        relation = TOKEN_LEQ;
                    else if (relation == TOKEN_GEQ)
                        relation = TOKEN_LESS;
                    else
                        relation = TOKEN_NONE;
                }
                bool increasing = relation == TOKEN_LESS || relation == TOKEN_LEQ;
                bool decreasing = relation == TOKEN_GREATER || relation == TOKEN_GEQ;
                std::map<std::string, int> assigned;
                bool continues = false;
                for (const auto& child : body) writes(child, assigned, continues);
                NodePtr update;
                for (const auto& child : body) {
                    if (child->get_type() == NODE_VARASSIGN && child->get_name() == counter) {
                        update = child->get_child()[0];
                    }
                }
                if (start.known && end.known && !start.array && !end.array &&
                    (increasing || decreasing) && assigned[counter] == 1 && !continues &&
                    scalar_expression(comparison[1]) && !reads_any(comparison[1], assigned) &&
                    update && update->get_type() == NODE_BINOP) {
                    auto operands = update->get_child();
                    auto amount = number(operands[1]);
                    TokenType op = update->get_tok()->get_type();
                    if (is_variable(operands[0], counter) && amount) {
                        long double delta = op == TOKEN_SUB ? -*amount : *amount;
                        if ((op == TOKEN_ADD || op == TOKEN_SUB) &&
                            ((increasing && delta > 0) || (decreasing && delta < 0))) {
                            iterations = {};
                            iterations.power = std::max(start.power, end.power);
                            iterations.notes.insert(
                                "Loop counter advances by a fixed amount toward an invariant "
                                "bound.");
                        } else if (op == TOKEN_MUL && *amount > 1 && increasing && start.constant &&
                                   *start.constant > 0) {
                            iterations = {};
                            iterations.logs = end.power > 0 ? 1 : 0;
                            iterations.notes.insert(
                                "Positive loop counter grows by a fixed factor toward an invariant "
                                "bound.");
                        } else if (op == TOKEN_DIV && *amount > 1 && decreasing && end.constant &&
                                   *end.constant > 0) {
                            iterations = {};
                            iterations.logs = start.power > 0 ? 1 : 0;
                            iterations.notes.insert(
                                "Loop counter shrinks by a fixed factor toward a positive "
                                "constant.");
                        }
                    }
                }
            }
        }
        Environment local = env;
        // The counter changes across iterations; do not keep its initial
        // constant when analyzing a nested loop that uses it as a bound.
        invalidate(local, body);
        Cost repeated = combine(statement(condition, local), block(body, local));
        invalidate(env, body);
        return combine(iterations, repeated, true);
    }

    Cost statement(const NodePtr& node, Environment& env) {
        if (!node) return {};
        if (--remaining <= 0) return Cost::unknown("Analysis work limit reached.");
        switch (node->get_type()) {
            case NODE_ALGODEF:
                if (!functions.count(node->get_name()) || functions.at(node->get_name()) != node) {
                    env[node->get_name()] = {};
                }
                return {};
            case NODE_STRUCTDEF:
                env[node->get_name()] = {};
                return {};
            case NODE_ERROR:
                return Cost::unknown("Invalid syntax.");
            case NODE_VARASSIGN: {
                Bound bound = value_bound(node->get_child()[0], env);
                Cost result = statement(node->get_child()[0], env);
                env[node->get_name()] = bound;
                return result;
            }
            case NODE_BINOP: {
                auto op = node->get_tok();
                if (op->get_type() != TOKEN_KEYWORD ||
                    (op->get_value() != "and" && op->get_value() != "or")) {
                    return block(children(node), env);
                }
                auto parts = node->get_child();
                Cost left = statement(parts[0], env);
                // Short-circuit evaluation may skip the right operand, so its
                // assignments cannot establish unconditional bounds.
                Environment right_env = env;
                Cost right = statement(parts[1], right_env);
                invalidate(env, {parts[1]});
                return combine(left, right);
            }
            case NODE_FOR:
                return for_cost(node, env);
            case NODE_WHILE:
            case NODE_REPEAT:
                return while_cost(node, env);
            case NODE_IF: {
                auto branch = std::static_pointer_cast<IfNode>(node);
                Cost condition = statement(branch->get_condition(), env);
                Environment yes = env;
                Environment no = env;
                Cost result =
                    combine(block(branch->get_expr(), yes), block(branch->get_else(), no));
                invalidate(env, branch->get_expr());
                invalidate(env, branch->get_else());
                return combine(condition, result);
            }
            case NODE_ALGOCALL:
                return call_cost(std::static_pointer_cast<AlgorithmCallNode>(node), env);
            case NODE_ARRASSIGN: {
                Cost result = block(children(node), env);
                invalidate(env, {node});
                return result;
            }
            default:
                return block(children(node), env);
        }
    }

    std::vector<Definition> definitions;
    std::map<std::string, NodePtr> functions;
    std::set<std::string> ambiguous;
    std::set<std::size_t> active;
    int remaining{100000};
};

}  // namespace

std::string analyze_complexity(const NodeList& ast) { return Analyzer(ast).report(ast); }

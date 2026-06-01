/// --------------------
/// Interpreter
/// --------------------

#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "symboltable.h"
#include "value.h"
#include "node.h"
#include "token.h"
#include "jit.h"
#include <memory>
#include <optional>
#include <unordered_map>

class Interpreter {
public:
    Interpreter(SymbolTable &symbols)
        : symbol_table(symbols) {}
    std::shared_ptr<Value> visit(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_number(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_var_access(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_var_assign(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_bin_op(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_unary_op(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_array(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_if(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_for(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_while(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_repeat(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_algo_def(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_struct_def(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_algo_call(std::shared_ptr<Node>);
    std::shared_ptr<Value>& visit_array_access(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_array_assign(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_member_access(std::shared_ptr<Node>);
    std::shared_ptr<Value> visit_return(std::shared_ptr<Node>);

    std::shared_ptr<Value> bin_op(std::shared_ptr<Value>, std::shared_ptr<Value>, std::shared_ptr<Token>);
    std::shared_ptr<Value> unary_op(std::shared_ptr<Value>, std::shared_ptr<Token>);
protected:
    struct JitCacheEntry {
        int hits{0};
        bool disabled{false};
        std::optional<JitProgram> program;
    };

    std::optional<std::shared_ptr<Value>> try_visit_jit(const std::shared_ptr<Node>& node);

    SymbolTable &symbol_table;
    std::shared_ptr<Value> error, algo_call_temp;
    std::unordered_map<const Node*, JitCacheEntry> jit_cache;
};

#endif

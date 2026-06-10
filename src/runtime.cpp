/// --------------------
/// Compiled-program runtime
/// --------------------

#include "runtime.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "color.h"
#include "interpreter.h"
#include "node.h"
#include "symboltable.h"
#include "value.h"

namespace {

// Arena frames keep every runtime-created value alive until its frame is
// released; survivors are owned by symbol tables and containers.
std::vector<std::vector<std::shared_ptr<Value>>> frames;
// Scope stack: scopes.back() is the current symbol table; entry 0 holds the
// globals. Parents follow the caller chain, like the interpreter.
std::vector<std::unique_ptr<SymbolTable>> scopes;
std::vector<std::unordered_map<int64_t, int64_t>> i64_memo_tables;

[[noreturn]] void rt_fail(const std::shared_ptr<Value>& err) {
    std::cout << err->get_num() << "\n";
    exit(1);
}

Value* track(std::shared_ptr<Value> value) {
    if (value->get_type() == VALUE_ERROR) {
        rt_fail(value);
    }
    frames.back().push_back(value);
    return value.get();
}

std::shared_ptr<Value> ref(Value* v) { return v->shared_from_this(); }

SymbolTable& current_scope() { return *scopes.back(); }

class CompiledAlgoValue : public Value {
   public:
    CompiledAlgoValue(const std::string& _algo_name, Value* (*_fn)(),
                      std::vector<std::string> _arg_names, bool _memoizable)
        : Value(VALUE_ALGO),
          fn(_fn),
          algo_name(_algo_name),
          arg_names(std::move(_arg_names)),
          memoizable(_memoizable) {}

    std::string get_num() override { return algo_name; }
    std::string repr() override { return algo_name; }

    std::shared_ptr<Value> execute(const NodeList& args = {},
                                   SymbolTable* parent = nullptr) override;

    Value* (*fn)();
    std::string algo_name;
    std::vector<std::string> arg_names;
    bool memoizable;
    std::unordered_map<std::string, std::shared_ptr<Value>> memo;
};

// Mirrors numeric_cache_key in pseudo.cpp; empty when any argument is not a
// plain number.
std::string numeric_args_key(const ValueList& values) {
    std::string key;
    for (const auto& value : values) {
        if (value->get_type() != VALUE_INT && value->get_type() != VALUE_FLOAT) return "";
        key += value->get_type();
        key += ':';
        key += value->get_num();
        key += '|';
    }
    return key;
}

std::shared_ptr<Value> make_compiled_algo(const char* name, Value* (*fn)(),
                                          const char* const* arg_names, int64_t nargs,
                                          int64_t memoizable) {
    std::vector<std::string> names;
    names.reserve(nargs);
    for (int64_t i = 0; i < nargs; ++i) {
        names.emplace_back(arg_names[i]);
    }
    return std::make_shared<CompiledAlgoValue>(name, fn, std::move(names), memoizable != 0);
}

void run_scope_destructors(SymbolTable& scope) {
    if (!scope.has_instances()) {
        return;
    }
    for (const auto& [name, val] : scope.get_symbols()) {
        (void)name;
        if (val->get_type() != VALUE_INSTANCE || val.use_count() != 1) {
            continue;
        }
        InstanceValue* inst = dynamic_cast<InstanceValue*>(val.get());
        std::shared_ptr<Value> self_ptr = val;
        std::shared_ptr<Value> dtor = inst->get_member("destructor", self_ptr);
        if (dtor->get_type() == VALUE_ALGO) {
            std::shared_ptr<Value> ret = dtor->execute({}, scope.get_parent());
            if (ret->get_type() == VALUE_ERROR) {
                rt_fail(ret);
            }
        }
    }
}

std::shared_ptr<Value> call_compiled(CompiledAlgoValue* algo, const ValueList& args,
                                     SymbolTable* parent = nullptr) {
    if (args.size() < algo->arg_names.size()) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too few arguments" RESET);
    }
    if (args.size() > algo->arg_names.size()) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too many arguments" RESET);
    }

    std::string memo_key;
    if (algo->memoizable) {
        memo_key = numeric_args_key(args);
        if (!memo_key.empty()) {
            auto cached = algo->memo.find(memo_key);
            if (cached != algo->memo.end()) {
                return cached->second;
            }
        }
    }

    int64_t mark = rt_frame_mark();
    rt_frame_push();
    scopes.push_back(std::make_unique<SymbolTable>(parent != nullptr ? parent : &current_scope()));
    for (size_t i = 0; i < args.size(); ++i) {
        scopes.back()->set(algo->arg_names[i], args[i]);
    }
    Value* result = algo->fn();
    std::shared_ptr<Value> kept = ref(result);
    rt_frame_release(mark);
    run_scope_destructors(*scopes.back());
    scopes.pop_back();
    if (!memo_key.empty() && (kept->get_type() == VALUE_INT || kept->get_type() == VALUE_FLOAT)) {
        algo->memo[memo_key] = kept;
    }
    return kept;
}

std::shared_ptr<Value> CompiledAlgoValue::execute(const NodeList& args, SymbolTable* parent) {
    SymbolTable sym(parent);
    Interpreter interpreter(sym);
    ValueList evaluated;
    evaluated.reserve(args.size());
    for (const auto& arg : args) {
        std::shared_ptr<Value> v = interpreter.visit(arg);
        if (v->get_type() == VALUE_ERROR) {
            return v;
        }
        evaluated.push_back(v);
    }
    return call_compiled(this, evaluated, &sym);
}

}  // namespace

extern "C" {

void rt_init() {
    frames.emplace_back();
    scopes.push_back(std::make_unique<SymbolTable>());
}

Value* rt_make_int(int64_t v) { return track(std::make_shared<TypedValue<int64_t>>(VALUE_INT, v)); }

Value* rt_make_float(double v) {
    return track(std::make_shared<TypedValue<double>>(VALUE_FLOAT, v));
}

Value* rt_make_string(const char* s) {
    return track(std::make_shared<TypedValue<std::string>>(VALUE_STRING, std::string(s)));
}

Value* rt_make_none() { return track(std::make_shared<Value>()); }

Value* rt_array_new() { return track(std::make_shared<ArrayValue>(ValueList(0))); }

void rt_array_push(Value* arr, Value* v) { dynamic_cast<ArrayValue*>(arr)->push_back(ref(v)); }

Value* rt_array_get_i64(Value* arr, int64_t index) {
    ArrayValue* array = dynamic_cast<ArrayValue*>(arr);
    if (array == nullptr) {
        rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Indexing a non-array value"));
    }
    return track(array->operator[](static_cast<int>(index)));
}

Value* rt_array_set_i64(Value* arr, int64_t index, int64_t value) {
    ArrayValue* array = dynamic_cast<ArrayValue*>(arr);
    if (array == nullptr) {
        rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Indexing a non-array value"));
    }
    std::shared_ptr<Value> boxed = std::make_shared<TypedValue<int64_t>>(VALUE_INT, value);
    array->operator[](static_cast<int>(index)) = boxed;
    if (boxed->get_type() == VALUE_ERROR) {
        rt_fail(boxed);
    }
    return track(boxed);
}

Value* rt_array_push_i64(Value* arr, int64_t value) {
    ArrayValue* array = dynamic_cast<ArrayValue*>(arr);
    if (array == nullptr) {
        rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Calling push on a non-array value"));
    }
    std::shared_ptr<Value> boxed = std::make_shared<TypedValue<int64_t>>(VALUE_INT, value);
    array->push_back(boxed);
    return track(boxed);
}

int64_t rt_array_pop_i64(Value* arr) {
    ArrayValue* array = dynamic_cast<ArrayValue*>(arr);
    if (array == nullptr) {
        rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Calling pop on a non-array value"));
    }
    std::shared_ptr<Value> value = array->pop_back();
    if (value->get_type() == VALUE_ERROR) {
        rt_fail(value);
    }
    return value->as_int();
}

int64_t rt_i64_memo_lookup(int64_t memo_id, int64_t arg, int64_t* out) {
    if (memo_id < 0) {
        return 0;
    }
    size_t index = static_cast<size_t>(memo_id);
    if (i64_memo_tables.size() <= index) {
        i64_memo_tables.resize(index + 1);
        return 0;
    }
    auto found = i64_memo_tables[index].find(arg);
    if (found == i64_memo_tables[index].end()) {
        return 0;
    }
    *out = found->second;
    return 1;
}

void rt_i64_memo_store(int64_t memo_id, int64_t arg, int64_t value) {
    if (memo_id < 0) {
        return;
    }
    size_t index = static_cast<size_t>(memo_id);
    if (i64_memo_tables.size() <= index) {
        i64_memo_tables.resize(index + 1);
    }
    i64_memo_tables[index][arg] = value;
}

Value* rt_get_var(const char* name) { return track(current_scope().get(name)); }

Value* rt_set_var(const char* name, Value* v) {
    current_scope().set(name, ref(v));
    return track(current_scope().get(name));
}

Value* rt_bin_op(int64_t op, Value* a, Value* b) {
    std::shared_ptr<Value> lhs = ref(a), rhs = ref(b);
    switch (op) {
        case RT_OP_ADD:
            return track(lhs + rhs);
        case RT_OP_SUB:
            return track(lhs - rhs);
        case RT_OP_MUL:
            return track(lhs * rhs);
        case RT_OP_DIV:
            return track(lhs / rhs);
        case RT_OP_MOD:
            return track(lhs % rhs);
        case RT_OP_POW:
            return track(pow(lhs, rhs));  // NOLINT(misc-include-cleaner): value.h overload
        case RT_OP_EQUAL:
            return track(lhs == rhs);
        case RT_OP_NEQ:
            return track(lhs != rhs);
        case RT_OP_LESS:
            return track(lhs < rhs);
        case RT_OP_GREATER:
            return track(lhs > rhs);
        case RT_OP_LEQ:
            return track(lhs <= rhs);
        case RT_OP_GEQ:
            return track(lhs >= rhs);
        default:
            rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Not a binary op\n"));
    }
}

Value* rt_unary_op(int64_t op, Value* a) {
    std::shared_ptr<Value> operand = ref(a);
    switch (op) {
        case RT_OP_UPLUS:
            return track(operand);
        case RT_OP_UNEG:
            return track(-operand);
        case RT_OP_UNOT:
            return track(!operand);
        default:
            rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Not an unary op\n"));
    }
}

Value* rt_bool(Value* v) { return rt_make_int(v->as_int() != 0); }

int64_t rt_cond_eq1(Value* v) { return std::stoll(v->get_num()) == 1; }

int64_t rt_as_int(Value* v) { return v->as_int(); }

int64_t rt_for_cond(Value* i, Value* end, Value* step) {
    if (step->as_double() > 0) {
        if (i->get_type() == VALUE_FLOAT || end->get_type() == VALUE_FLOAT)
            return i->as_double() <= end->as_double();
        return i->as_int() <= end->as_int();
    }
    if (i->get_type() == VALUE_FLOAT || end->get_type() == VALUE_FLOAT)
        return i->as_double() >= end->as_double();
    return i->as_int() >= end->as_int();
}

void rt_for_step_check(Value* step) {
    if (step->as_double() == 0) {
        rt_fail(std::make_shared<ErrorValue>(VALUE_ERROR, "Infinite for loop\n"));
    }
}

Value* rt_index(Value* obj, Value* idx) {
    std::shared_ptr<Value> container = ref(obj), index = ref(idx);
    if (container->get_type() == VALUE_STRING) {
        std::string str = container->as_string();
        int p = index->as_int();
        if (1 <= p && p <= static_cast<int>(str.size())) {
            return track(std::make_shared<TypedValue<std::string>>(VALUE_STRING,
                                                                   std::string(1, str[p - 1])));
        }
        return track(std::make_shared<ErrorValue>(
            VALUE_ERROR, "Index out of range, size: " + std::to_string(str.size()) +
                             ", position: " + std::to_string(p)));
    }
    if (container->get_type() == VALUE_HASH_TABLE) {
        return track(dynamic_cast<HashTableValue*>(container.get())->get(index));
    }
    if (container->get_type() != VALUE_ARRAY) {
        return track(std::make_shared<ErrorValue>(
            VALUE_ERROR, "Access can only apply on array, find " + container->get_type() + "\n"));
    }
    return track(dynamic_cast<ArrayValue*>(container.get())->operator[](index->as_int()));
}

Value* rt_index_assign(Value* obj, Value* idx, Value* v) {
    std::shared_ptr<Value> container = ref(obj), index = ref(idx), value = ref(v);
    if (container->get_type() == VALUE_HASH_TABLE) {
        return track(dynamic_cast<HashTableValue*>(container.get())->set(index, value));
    }
    if (container->get_type() != VALUE_ARRAY) {
        return track(std::make_shared<ErrorValue>(
            VALUE_ERROR, "Access can only apply on array or object member\n"));
    }
    // Matches visit_array_assign: an out-of-range index assigns into the
    // array's error slot and is effectively ignored.
    dynamic_cast<ArrayValue*>(container.get())->operator[](index->as_int()) = value;
    return track(value);
}

Value* rt_member_access(Value* obj, const char* name) {
    std::shared_ptr<Value> object = ref(obj);
    if (object->get_type() == VALUE_ARRAY || object->get_type() == VALUE_STRING ||
        object->get_type() == VALUE_HASH_TABLE) {
        return track(std::make_shared<BoundMethodValue>(object, name));
    }
    if (object->get_type() == VALUE_INSTANCE) {
        return track(dynamic_cast<InstanceValue*>(object.get())->get_member(name, object));
    }
    return track(std::make_shared<ErrorValue>(
        VALUE_ERROR, object->get_num() + " has no member " + std::string(name) + "\n"));
}

Value* rt_member_assign(Value* obj, const char* name, Value* v) {
    std::shared_ptr<Value> object = ref(obj), value = ref(v);
    if (object->get_type() == VALUE_INSTANCE) {
        dynamic_cast<InstanceValue*>(object.get())->set_member(name, value);
        return track(value);
    }
    return track(std::make_shared<ErrorValue>(
        VALUE_ERROR, "Assignment to member only supported for Struct Instances\n"));
}

Value* rt_make_algo(const char* name, Value* (*fn)(), const char* const* arg_names, int64_t nargs,
                    int64_t memoizable) {
    return track(make_compiled_algo(name, fn, arg_names, nargs, memoizable));
}

Value* rt_define_algo(const char* name, Value* (*fn)(), const char* const* arg_names, int64_t nargs,
                      int64_t memoizable) {
    std::shared_ptr<Value> algo = make_compiled_algo(name, fn, arg_names, nargs, memoizable);
    current_scope().set(name, algo);
    return track(algo);
}

Value* rt_define_struct(const char* name, const char* const* member_names, int64_t nmembers,
                        const char* const* method_names, Value** methods, int64_t nmethods) {
    std::vector<std::string> members;
    members.reserve(nmembers);
    for (int64_t i = 0; i < nmembers; ++i) {
        members.emplace_back(member_names[i]);
    }

    std::map<std::string, std::shared_ptr<Value>> method_map;
    for (int64_t i = 0; i < nmethods; ++i) {
        method_map[method_names[i]] = ref(methods[i]);
    }

    std::shared_ptr<Value> struct_value = std::make_shared<StructValue>(name, members, method_map);
    current_scope().set(name, struct_value);
    return track(struct_value);
}

Value* rt_struct_add_method(const char* struct_name, const char* method_name, Value* method) {
    std::shared_ptr<Value> struct_value = current_scope().get(struct_name);
    if (struct_value->get_type() == VALUE_STRUCT) {
        dynamic_cast<StructValue*>(struct_value.get())->methods[method_name] = ref(method);
    }
    return track(ref(method));
}

Value* rt_call(Value* callee, Value** argv, int64_t argc) {
    ValueList args;
    args.reserve(argc);
    for (int64_t i = 0; i < argc; ++i) {
        args.push_back(ref(argv[i]));
    }

    if (auto* compiled = dynamic_cast<CompiledAlgoValue*>(callee)) {
        return track(call_compiled(compiled, args));
    }

    // Builtins, bound methods, and other interpreter-executed callees: hand
    // over the already-evaluated arguments as precomputed AST nodes.
    NodeList arg_nodes;
    arg_nodes.reserve(argc);
    for (const auto& arg : args) {
        arg_nodes.push_back(std::make_shared<PrecomputedNode>(arg));
    }
    return track(callee->execute(arg_nodes, &current_scope()));
}

int64_t rt_frame_mark() { return static_cast<int64_t>(frames.size()); }

void rt_frame_push() { frames.emplace_back(); }

void rt_frame_release(int64_t mark) {
    while (static_cast<int64_t>(frames.size()) > mark) {
        frames.pop_back();
    }
}

void rt_loop_keep(int64_t frame_index, Value* v) {
    std::shared_ptr<Value> kept = ref(v);
    frames[frame_index].clear();
    frames[frame_index].push_back(kept);
}

}  // extern "C"

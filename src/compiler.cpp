/// --------------------
/// LLVM AOT compiler
/// --------------------
///
/// Lowers the AST to LLVM IR. Every language operation becomes a call into
/// the runtime (src/runtime.h); control flow becomes real basic blocks. The
/// emitted code mirrors Interpreter::visit_* semantics; see
/// docs/superpowers/specs/2026-06-10-llvm-compiler-design.md.

#include "compiler.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis.h"
#include "node.h"
#include "runtime.h"
#include "token.h"

namespace {

class CodeGen {
   public:
    CodeGen(llvm::Module& _module, std::vector<std::string>& _errors)
        : module(_module), ctx(_module.getContext()), builder(ctx), errors(_errors) {
        ptr_ty = builder.getPtrTy();
        i64_ty = builder.getInt64Ty();
        f64_ty = builder.getDoubleTy();
    }

    bool run(const NodeList& ast) {
        llvm::Function* main_fn =
            llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), false),
                                   llvm::Function::ExternalLinkage, "main", module);
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx, "entry", main_fn);
        builder.SetInsertPoint(entry);
        builder.CreateCall(get_rt("rt_init", builder.getVoidTy(), {}));

        for (const auto& node : ast) {
            if (gen(node) == nullptr && block_terminated()) {
                break;
            }
        }
        if (!block_terminated()) {
            builder.CreateRet(builder.getInt32(0));
        }
        return errors.empty();
    }

   private:
    struct LoopContext {
        llvm::BasicBlock* latch;
        llvm::BasicBlock* exit;
        llvm::Value* iter_mark;
        // `while` re-pushes its iteration frame in the header, so `continue`
        // must release first; `for`/`repeat` latches release it themselves.
        bool release_on_continue;
    };

    struct NativeI64Var {
        llvm::AllocaInst* slot;
        bool dirty;
    };

    struct NativeI64Algo {
        llvm::Function* fn;
        size_t arity;
    };

    llvm::Module& module;
    llvm::LLVMContext& ctx;
    llvm::IRBuilder<> builder;
    std::vector<std::string>& errors;

    llvm::PointerType* ptr_ty{nullptr};
    llvm::Type* i64_ty{nullptr};
    llvm::Type* f64_ty{nullptr};

    std::vector<LoopContext> loops;
    std::unordered_map<std::string, NativeI64Var> native_i64_vars;
    std::unordered_map<std::string, NativeI64Algo> native_i64_algos;
    std::unordered_set<std::string> known_arrays;
    std::unordered_set<std::string> numeric_arrays;
    std::map<std::string, llvm::Value*> string_constants;
    int algo_counter{0};
    int native_i64_memo_counter{0};
    bool native_i64_enabled{true};
    std::optional<int64_t> active_i64_memo_id;
    llvm::Value* active_i64_memo_arg{nullptr};

    /// ---- helpers ----

    llvm::FunctionCallee get_rt(const std::string& name, llvm::Type* ret,
                                std::vector<llvm::Type*> args) {
        return module.getOrInsertFunction(name, llvm::FunctionType::get(ret, args, false));
    }

    bool block_terminated() { return builder.GetInsertBlock()->getTerminator() != nullptr; }

    llvm::Value* cstring(const std::string& text) {
        auto found = string_constants.find(text);
        if (found != string_constants.end()) {
            return found->second;
        }
        llvm::Value* global = builder.CreateGlobalString(text, "str");
        string_constants[text] = global;
        return global;
    }

    llvm::AllocaInst* entry_alloca(llvm::Type* type) {
        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return tmp.CreateAlloca(type);
    }

    llvm::Value* string_array(const std::vector<std::string>& strings,
                              const std::string& global_name) {
        if (strings.empty()) {
            return llvm::ConstantPointerNull::get(ptr_ty);
        }

        llvm::ArrayType* strings_ty = llvm::ArrayType::get(ptr_ty, strings.size());
        std::vector<llvm::Constant*> string_ptrs;
        string_ptrs.reserve(strings.size());
        for (const auto& value : strings) {
            string_ptrs.push_back(llvm::cast<llvm::Constant>(cstring(value)));
        }
        return new llvm::GlobalVariable(module, strings_ty, true, llvm::GlobalValue::PrivateLinkage,
                                        llvm::ConstantArray::get(strings_ty, string_ptrs),
                                        global_name);
    }

    llvm::Value* value_array(const std::vector<llvm::Value*>& values) {
        if (values.empty()) {
            return llvm::ConstantPointerNull::get(ptr_ty);
        }

        llvm::ArrayType* values_ty = llvm::ArrayType::get(ptr_ty, values.size());
        llvm::AllocaInst* slots = entry_alloca(values_ty);
        for (size_t i = 0; i < values.size(); ++i) {
            builder.CreateStore(values[i],
                                builder.CreateConstInBoundsGEP2_64(values_ty, slots, 0, i));
        }
        return slots;
    }

    void error(const std::string& message, const std::shared_ptr<Node>& node) {
        std::string suffix;
        std::shared_ptr<Token> tok = node ? node->get_tok() : nullptr;
        if (tok) {
            suffix = " (line " + std::to_string(tok->get_pos().line + 1) + ")";
        }
        errors.push_back(message + suffix);
    }

    llvm::Value* make_none() { return builder.CreateCall(get_rt("rt_make_none", ptr_ty, {})); }

    llvm::Value* make_int(int64_t v) {
        return builder.CreateCall(get_rt("rt_make_int", ptr_ty, {i64_ty}), {builder.getInt64(v)});
    }

    llvm::Value* box_i64(llvm::Value* value) {
        return builder.CreateCall(get_rt("rt_make_int", ptr_ty, {i64_ty}), {value});
    }

    llvm::AllocaInst* ensure_i64_slot(const std::string& name) {
        auto found = native_i64_vars.find(name);
        if (found != native_i64_vars.end()) {
            return found->second.slot;
        }
        llvm::AllocaInst* slot = entry_alloca(i64_ty);
        native_i64_vars.emplace(name, NativeI64Var{slot, false});
        return slot;
    }

    void flush_native_var(const std::string& name) {
        auto found = native_i64_vars.find(name);
        if (found == native_i64_vars.end() || !found->second.dirty || block_terminated()) {
            return;
        }
        llvm::Value* value = builder.CreateLoad(i64_ty, found->second.slot);
        builder.CreateCall(get_rt("rt_set_var", ptr_ty, {ptr_ty, ptr_ty}),
                           {cstring(name), box_i64(value)});
        found->second.dirty = false;
    }

    void flush_native_vars() {
        std::vector<std::string> names;
        names.reserve(native_i64_vars.size());
        for (const auto& [name, var] : native_i64_vars) {
            if (var.dirty) {
                names.push_back(name);
            }
        }
        for (const auto& name : names) {
            flush_native_var(name);
        }
    }

    void clear_native_locals() {
        native_i64_vars.clear();
        known_arrays.clear();
        numeric_arrays.clear();
    }

    llvm::Value* frame_mark() { return builder.CreateCall(get_rt("rt_frame_mark", i64_ty, {})); }

    void frame_push() { builder.CreateCall(get_rt("rt_frame_push", builder.getVoidTy(), {})); }

    void frame_release(llvm::Value* mark) {
        builder.CreateCall(get_rt("rt_frame_release", builder.getVoidTy(), {i64_ty}), {mark});
    }

    /// ---- dispatch ----

    // Emits code for one node. Returns the node's value (an LLVM `ptr` to a
    // runtime Value) or nullptr when the node terminated the current block
    // (break/continue/return) or reported a compile error.
    llvm::Value* gen(const std::shared_ptr<Node>& node) {
        std::string type = node->get_type();
        if (type == NODE_VALUE) return gen_literal(node);
        if (type == NODE_VARACCESS) return gen_var_access(node);
        if (type == NODE_VARASSIGN) return gen_var_assign(node);
        if (type == NODE_BINOP) return gen_bin_op(node);
        if (type == NODE_UNARYOP) return gen_unary_op(node);
        if (type == NODE_IF) return gen_if(node);
        if (type == NODE_FOR) return gen_for(node);
        if (type == NODE_WHILE) return gen_while(node);
        if (type == NODE_REPEAT) return gen_repeat(node);
        if (type == NODE_ALGODEF) return gen_algo_def(node);
        if (type == NODE_ALGOCALL) return gen_algo_call(node);
        if (type == NODE_ARRAY) return gen_array(node);
        if (type == NODE_ARRACCESS) return gen_array_access(node);
        if (type == NODE_ARRASSIGN) return gen_array_assign(node);
        if (type == NODE_MEMACCESS) return gen_member_access(node);
        if (type == NODE_RETURN) return gen_return(node);
        if (type == NODE_BREAK) return gen_loop_jump(node, true);
        if (type == NODE_CONTINUE) return gen_loop_jump(node, false);
        if (type == NODE_STRUCTDEF) return gen_struct_def(node);
        error("compile error: unsupported statement " + type, node);
        return nullptr;
    }

    // Emits a statement sequence; returns the last statement's value, or
    // nullptr when the sequence terminated the block (or errored).
    llvm::Value* gen_statements(const NodeList& nodes) {
        llvm::Value* last = nullptr;
        for (const auto& node : nodes) {
            last = gen(node);
            if (last == nullptr) {
                return nullptr;
            }
        }
        return last;
    }

    /// ---- expressions ----

    llvm::Value* gen_literal(const std::shared_ptr<Node>& node) {
        std::shared_ptr<Token> tok = node->get_tok();
        if (tok->get_type() == TOKEN_INT) {
            return make_int(std::stoll(tok->get_value()));
        }
        if (tok->get_type() == TOKEN_FLOAT) {
            return builder.CreateCall(get_rt("rt_make_float", ptr_ty, {f64_ty}),
                                      {llvm::ConstantFP::get(f64_ty, std::stod(tok->get_value()))});
        }
        if (tok->get_type() == TOKEN_STRING) {
            return builder.CreateCall(get_rt("rt_make_string", ptr_ty, {ptr_ty}),
                                      {cstring(tok->get_value())});
        }
        error("compile error: unsupported literal " + tok->get_type(), node);
        return nullptr;
    }

    llvm::Value* gen_var_access(const std::shared_ptr<Node>& node) {
        auto native = native_i64_vars.find(node->get_name());
        if (native != native_i64_vars.end()) {
            return box_i64(builder.CreateLoad(i64_ty, native->second.slot));
        }
        return builder.CreateCall(get_rt("rt_get_var", ptr_ty, {ptr_ty}),
                                  {cstring(node->get_name())});
    }

    llvm::Value* gen_var_assign(const std::shared_ptr<Node>& node) {
        const std::string name = node->get_name();
        std::shared_ptr<Node> rhs = node->get_child()[0];
        if (can_i64_expr(rhs)) {
            llvm::Value* value = gen_i64_expr(rhs);
            llvm::AllocaInst* slot = ensure_i64_slot(name);
            builder.CreateStore(value, slot);
            native_i64_vars[name].dirty = true;
            known_arrays.erase(name);
            numeric_arrays.erase(name);
            return box_i64(value);
        }

        llvm::Value* value = gen(rhs);
        if (value == nullptr) {
            return nullptr;
        }
        native_i64_vars.erase(name);
        if (rhs->get_type() == NODE_ARRAY) {
            known_arrays.insert(name);
            if (is_numeric_array_literal(rhs)) {
                numeric_arrays.insert(name);
            } else {
                numeric_arrays.erase(name);
            }
        } else {
            known_arrays.erase(name);
            numeric_arrays.erase(name);
        }
        return builder.CreateCall(get_rt("rt_set_var", ptr_ty, {ptr_ty, ptr_ty}),
                                  {cstring(name), value});
    }

    llvm::Value* as_int(llvm::Value* value) {
        return builder.CreateCall(get_rt("rt_as_int", i64_ty, {ptr_ty}), {value});
    }

    std::optional<int64_t> int_literal(const std::shared_ptr<Node>& node) {
        if (!node || node->get_type() != NODE_VALUE) {
            return std::nullopt;
        }
        std::shared_ptr<Token> tok = node->get_tok();
        if (tok->get_type() != TOKEN_INT) {
            return std::nullopt;
        }
        return std::stoll(tok->get_value());
    }

    bool known_nonzero_i64(const std::shared_ptr<Node>& node) {
        std::optional<int64_t> literal = int_literal(node);
        return literal && *literal != 0;
    }

    bool is_numeric_array_literal(const std::shared_ptr<Node>& node) {
        if (!node || node->get_type() != NODE_ARRAY) {
            return false;
        }
        for (const auto& element : node->get_child()) {
            if (!can_i64_expr(element)) {
                return false;
            }
        }
        return true;
    }

    bool is_direct_array_method_call(const std::shared_ptr<Node>& node, std::string& array_name,
                                     std::string& method_name, NodeList& args) {
        if (!node || node->get_type() != NODE_ALGOCALL) {
            return false;
        }
        AlgorithmCallNode* call = dynamic_cast<AlgorithmCallNode*>(node.get());
        std::shared_ptr<Node> callee = call->get_call();
        if (callee->get_type() != NODE_MEMACCESS) {
            return false;
        }
        NodeList member_child = callee->get_child();
        if (member_child.size() != 2 || member_child[0]->get_type() != NODE_VARACCESS) {
            return false;
        }
        array_name = member_child[0]->get_name();
        method_name = member_child[1]->get_name();
        args = call->get_args();
        return known_arrays.count(array_name) != 0;
    }

    bool can_i64_expr(const std::shared_ptr<Node>& node,
                      const std::unordered_set<std::string>* allowed_vars = nullptr,
                      const std::string& self_name = "", size_t self_arity = 0) {
        if (!node) return false;
        if (!native_i64_enabled && allowed_vars == nullptr) return false;

        std::string type = node->get_type();
        if (type == NODE_VALUE) {
            return node->get_tok()->get_type() == TOKEN_INT;
        }
        if (type == NODE_VARACCESS) {
            if (allowed_vars != nullptr) {
                return allowed_vars->count(node->get_name()) != 0;
            }
            return native_i64_vars.count(node->get_name()) != 0;
        }
        if (type == NODE_UNARYOP) {
            std::shared_ptr<Token> op = node->get_tok();
            if (op->get_type() != TOKEN_ADD && op->get_type() != TOKEN_SUB &&
                !(op->get_type() == TOKEN_KEYWORD && op->get_value() == "not")) {
                return false;
            }
            NodeList child = node->get_child();
            return child.size() == 1 && can_i64_expr(child[0], allowed_vars, self_name, self_arity);
        }
        if (type == NODE_BINOP) {
            NodeList child = node->get_child();
            if (child.size() != 2 || !can_i64_expr(child[0], allowed_vars, self_name, self_arity) ||
                !can_i64_expr(child[1], allowed_vars, self_name, self_arity)) {
                return false;
            }
            std::shared_ptr<Token> op = node->get_tok();
            const std::string& op_type = op->get_type();
            if (op_type == TOKEN_ADD || op_type == TOKEN_SUB || op_type == TOKEN_MUL ||
                op_type == TOKEN_EQUAL || op_type == TOKEN_NEQ || op_type == TOKEN_LESS ||
                op_type == TOKEN_GREATER || op_type == TOKEN_LEQ || op_type == TOKEN_GEQ) {
                return true;
            }
            if ((op_type == TOKEN_DIV || op_type == TOKEN_MOD) && known_nonzero_i64(child[1])) {
                return true;
            }
            return op_type == TOKEN_KEYWORD && (op->get_value() == "and" || op->get_value() == "or");
        }
        if (type == NODE_ARRACCESS && allowed_vars == nullptr) {
            NodeList child = node->get_child();
            return child.size() == 2 && child[0]->get_type() == NODE_VARACCESS &&
                   numeric_arrays.count(child[0]->get_name()) != 0 && can_i64_expr(child[1]);
        }
        if (type == NODE_ALGOCALL) {
            if (allowed_vars == nullptr) {
                std::string array_name, method_name;
                NodeList method_args;
                if (is_direct_array_method_call(node, array_name, method_name, method_args) &&
                    (method_name == "pop" || method_name == "pop_back") && method_args.empty() &&
                    numeric_arrays.count(array_name) != 0) {
                    return true;
                }
            }
            AlgorithmCallNode* call = dynamic_cast<AlgorithmCallNode*>(node.get());
            if (call->get_call()->get_type() != NODE_VARACCESS) {
                return false;
            }
            const std::string name = call->get_name();
            const NodeList& args = call->get_args();
            bool known = false;
            size_t arity = 0;
            if (!self_name.empty() && name == self_name) {
                known = true;
                arity = self_arity;
            } else {
                auto found = native_i64_algos.find(name);
                if (found != native_i64_algos.end()) {
                    known = true;
                    arity = found->second.arity;
                }
            }
            if (!known || args.size() != arity) {
                return false;
            }
            for (const auto& arg : args) {
                if (!can_i64_expr(arg, allowed_vars, self_name, self_arity)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    llvm::Value* gen_i64_expr(const std::shared_ptr<Node>& node) {
        std::string type = node->get_type();
        if (type == NODE_VALUE) {
            return builder.getInt64(*int_literal(node));
        }
        if (type == NODE_VARACCESS) {
            return builder.CreateLoad(i64_ty, native_i64_vars.at(node->get_name()).slot);
        }
        if (type == NODE_UNARYOP) {
            llvm::Value* operand = gen_i64_expr(node->get_child()[0]);
            std::shared_ptr<Token> op = node->get_tok();
            if (op->get_type() == TOKEN_SUB) {
                return builder.CreateNeg(operand);
            }
            if (op->get_type() == TOKEN_KEYWORD && op->get_value() == "not") {
                return builder.CreateZExt(builder.CreateICmpEQ(operand, builder.getInt64(0)), i64_ty);
            }
            return operand;
        }
        if (type == NODE_BINOP) {
            NodeList child = node->get_child();
            llvm::Value* lhs = gen_i64_expr(child[0]);
            llvm::Value* rhs = gen_i64_expr(child[1]);
            std::shared_ptr<Token> op = node->get_tok();
            const std::string& op_type = op->get_type();
            if (op_type == TOKEN_ADD) return builder.CreateAdd(lhs, rhs);
            if (op_type == TOKEN_SUB) return builder.CreateSub(lhs, rhs);
            if (op_type == TOKEN_MUL) return builder.CreateMul(lhs, rhs);
            if (op_type == TOKEN_DIV) return builder.CreateSDiv(lhs, rhs);
            if (op_type == TOKEN_MOD) return builder.CreateSRem(lhs, rhs);
            if (op_type == TOKEN_EQUAL) return builder.CreateZExt(builder.CreateICmpEQ(lhs, rhs), i64_ty);
            if (op_type == TOKEN_NEQ) return builder.CreateZExt(builder.CreateICmpNE(lhs, rhs), i64_ty);
            if (op_type == TOKEN_LESS) return builder.CreateZExt(builder.CreateICmpSLT(lhs, rhs), i64_ty);
            if (op_type == TOKEN_GREATER) return builder.CreateZExt(builder.CreateICmpSGT(lhs, rhs), i64_ty);
            if (op_type == TOKEN_LEQ) return builder.CreateZExt(builder.CreateICmpSLE(lhs, rhs), i64_ty);
            if (op_type == TOKEN_GEQ) return builder.CreateZExt(builder.CreateICmpSGE(lhs, rhs), i64_ty);
            if (op_type == TOKEN_KEYWORD && op->get_value() == "and") {
                return builder.CreateZExt(
                    builder.CreateAnd(builder.CreateICmpNE(lhs, builder.getInt64(0)),
                                      builder.CreateICmpNE(rhs, builder.getInt64(0))),
                    i64_ty);
            }
            if (op_type == TOKEN_KEYWORD && op->get_value() == "or") {
                return builder.CreateZExt(
                    builder.CreateOr(builder.CreateICmpNE(lhs, builder.getInt64(0)),
                                     builder.CreateICmpNE(rhs, builder.getInt64(0))),
                    i64_ty);
            }
        }
        if (type == NODE_ARRACCESS) {
            NodeList child = node->get_child();
            std::string array_name = child[0]->get_name();
            llvm::Value* container = gen_direct_array_var(array_name);
            llvm::Value* index = gen_i64_expr(child[1]);
            llvm::Value* value =
                builder.CreateCall(get_rt("rt_array_get_i64", ptr_ty, {ptr_ty, i64_ty}),
                                   {container, index});
            return as_int(value);
        }
        if (type == NODE_ALGOCALL) {
            std::string array_name, method_name;
            NodeList method_args;
            if (is_direct_array_method_call(node, array_name, method_name, method_args) &&
                (method_name == "pop" || method_name == "pop_back") && method_args.empty() &&
                numeric_arrays.count(array_name) != 0) {
                return builder.CreateCall(get_rt("rt_array_pop_i64", i64_ty, {ptr_ty}),
                                          {gen_direct_array_var(array_name)});
            }
            AlgorithmCallNode* call = dynamic_cast<AlgorithmCallNode*>(node.get());
            const std::string name = call->get_name();
            llvm::Function* fn = native_i64_algos.at(name).fn;
            std::vector<llvm::Value*> args;
            args.reserve(call->get_args().size());
            for (const auto& arg : call->get_args()) {
                args.push_back(gen_i64_expr(arg));
            }
            return builder.CreateCall(fn, args);
        }
        error("internal compiler error: invalid native integer expression", node);
        return builder.getInt64(0);
    }

    llvm::Value* gen_bin_op(const std::shared_ptr<Node>& node) {
        if (can_i64_expr(node)) {
            return box_i64(gen_i64_expr(node));
        }

        std::shared_ptr<Token> op = node->get_tok();
        if (op->get_type() == TOKEN_KEYWORD &&
            (op->get_value() == "and" || op->get_value() == "or")) {
            return gen_short_circuit(node, op->get_value() == "and");
        }

        static const std::map<std::string, int64_t> OP_CODES{
            {TOKEN_ADD, RT_OP_ADD},         {TOKEN_SUB, RT_OP_SUB}, {TOKEN_MUL, RT_OP_MUL},
            {TOKEN_DIV, RT_OP_DIV},         {TOKEN_MOD, RT_OP_MOD}, {TOKEN_POW, RT_OP_POW},
            {TOKEN_EQUAL, RT_OP_EQUAL},     {TOKEN_NEQ, RT_OP_NEQ}, {TOKEN_LESS, RT_OP_LESS},
            {TOKEN_GREATER, RT_OP_GREATER}, {TOKEN_LEQ, RT_OP_LEQ}, {TOKEN_GEQ, RT_OP_GEQ},
        };
        auto code = OP_CODES.find(op->get_type());
        if (code == OP_CODES.end()) {
            error("compile error: unsupported operator " + op->get_type(), node);
            return nullptr;
        }

        NodeList child = node->get_child();
        llvm::Value* lhs = gen(child[0]);
        if (lhs == nullptr) return nullptr;
        llvm::Value* rhs = gen(child[1]);
        if (rhs == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_bin_op", ptr_ty, {i64_ty, ptr_ty, ptr_ty}),
                                  {builder.getInt64(code->second), lhs, rhs});
    }

    // Mirrors visit_bin_op's short-circuit `and` / `or`: the result is always
    // an Int 0/1 derived from as_int().
    llvm::Value* gen_short_circuit(const std::shared_ptr<Node>& node, bool is_and) {
        NodeList child = node->get_child();
        llvm::Value* lhs = gen(child[0]);
        if (lhs == nullptr) return nullptr;

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* rhs_bb = llvm::BasicBlock::Create(ctx, "sc.rhs", fn);
        llvm::BasicBlock* short_bb = llvm::BasicBlock::Create(ctx, "sc.short", fn);
        llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "sc.merge", fn);

        llvm::Value* lhs_true = builder.CreateICmpNE(as_int(lhs), builder.getInt64(0));
        if (is_and) {
            builder.CreateCondBr(lhs_true, rhs_bb, short_bb);
        } else {
            builder.CreateCondBr(lhs_true, short_bb, rhs_bb);
        }

        builder.SetInsertPoint(short_bb);
        llvm::Value* short_value = make_int(is_and ? 0 : 1);
        builder.CreateBr(merge_bb);

        builder.SetInsertPoint(rhs_bb);
        llvm::Value* rhs = gen(child[1]);
        if (rhs == nullptr) return nullptr;
        llvm::Value* rhs_value = builder.CreateCall(get_rt("rt_bool", ptr_ty, {ptr_ty}), {rhs});
        llvm::BasicBlock* rhs_end = builder.GetInsertBlock();
        builder.CreateBr(merge_bb);

        builder.SetInsertPoint(merge_bb);
        llvm::PHINode* phi = builder.CreatePHI(ptr_ty, 2);
        phi->addIncoming(short_value, short_bb);
        phi->addIncoming(rhs_value, rhs_end);
        return phi;
    }

    llvm::Value* gen_unary_op(const std::shared_ptr<Node>& node) {
        if (can_i64_expr(node)) {
            return box_i64(gen_i64_expr(node));
        }

        std::shared_ptr<Token> op = node->get_tok();
        int64_t code;
        if (op->get_type() == TOKEN_ADD) {
            code = RT_OP_UPLUS;
        } else if (op->get_type() == TOKEN_SUB) {
            code = RT_OP_UNEG;
        } else if (op->get_type() == TOKEN_KEYWORD && op->get_value() == "not") {
            code = RT_OP_UNOT;
        } else {
            error("compile error: unsupported unary operator " + op->get_type(), node);
            return nullptr;
        }
        llvm::Value* operand = gen(node->get_child()[0]);
        if (operand == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_unary_op", ptr_ty, {i64_ty, ptr_ty}),
                                  {builder.getInt64(code), operand});
    }

    /// ---- control flow ----

    bool can_native_for_stmt(const std::shared_ptr<Node>& stmt, const std::string& loop_var) {
        if (stmt->get_type() == NODE_VARASSIGN) {
            return stmt->get_name() != loop_var && can_i64_expr(stmt->get_child()[0]);
        }
        if (stmt->get_type() == NODE_ARRASSIGN) {
            NodeList child = stmt->get_child();
            if (child[0]->get_type() != NODE_ARRACCESS) {
                return false;
            }
            NodeList access_child = child[0]->get_child();
            if (access_child.size() != 2 || access_child[0]->get_type() != NODE_VARACCESS ||
                known_arrays.count(access_child[0]->get_name()) == 0) {
                return false;
            }
            return can_i64_expr(access_child[1]) && can_i64_expr(child[1]);
        }
        if (stmt->get_type() == NODE_ALGOCALL) {
            std::string array_name, method_name;
            NodeList args;
            return is_direct_array_method_call(stmt, array_name, method_name, args) &&
                   (method_name == "push" || method_name == "push_back") && args.size() == 1 &&
                   can_i64_expr(args[0]);
        }
        return false;
    }

    llvm::Value* gen_direct_array_var(const std::string& array_name) {
        return builder.CreateCall(get_rt("rt_get_var", ptr_ty, {ptr_ty}), {cstring(array_name)});
    }

    bool gen_native_for_stmt(const std::shared_ptr<Node>& stmt) {
        if (stmt->get_type() == NODE_VARASSIGN) {
            const std::string name = stmt->get_name();
            llvm::Value* value = gen_i64_expr(stmt->get_child()[0]);
            llvm::AllocaInst* slot = ensure_i64_slot(name);
            builder.CreateStore(value, slot);
            native_i64_vars[name].dirty = true;
            known_arrays.erase(name);
            return true;
        }
        if (stmt->get_type() == NODE_ARRASSIGN) {
            NodeList child = stmt->get_child();
            NodeList access_child = child[0]->get_child();
            std::string array_name = access_child[0]->get_name();
            llvm::Value* container = gen_direct_array_var(array_name);
            llvm::Value* index = gen_i64_expr(access_child[1]);
            llvm::Value* value = gen_i64_expr(child[1]);
            builder.CreateCall(get_rt("rt_array_set_i64", ptr_ty, {ptr_ty, i64_ty, i64_ty}),
                               {container, index, value});
            return true;
        }
        if (stmt->get_type() == NODE_ALGOCALL) {
            std::string array_name, method_name;
            NodeList args;
            if (!is_direct_array_method_call(stmt, array_name, method_name, args)) {
                return false;
            }
            llvm::Value* container = gen_direct_array_var(array_name);
            llvm::Value* value = gen_i64_expr(args[0]);
            builder.CreateCall(get_rt("rt_array_push_i64", ptr_ty, {ptr_ty, i64_ty}),
                               {container, value});
            return true;
        }
        return false;
    }

    bool can_native_for(const NodeList& child, const std::string& var_name) {
        if (child.size() < 4 || child[0]->get_type() != NODE_VARASSIGN ||
            !can_i64_expr(child[0]->get_child()[0])) {
            return false;
        }

        auto existing = native_i64_vars.find(var_name);
        bool had_existing = existing != native_i64_vars.end();
        NativeI64Var saved{nullptr, false};
        if (had_existing) {
            saved = existing->second;
        } else {
            native_i64_vars.emplace(var_name, NativeI64Var{nullptr, false});
        }

        bool ok = (child[2] == nullptr || can_i64_expr(child[2])) && can_i64_expr(child[1]);
        for (size_t i = 3; ok && i < child.size(); ++i) {
            ok = can_native_for_stmt(child[i], var_name);
        }

        if (had_existing) {
            native_i64_vars[var_name] = saved;
        } else {
            native_i64_vars.erase(var_name);
        }
        return ok;
    }

    llvm::Value* gen_native_for(const std::shared_ptr<Node>& node) {
        NodeList child = node->get_child();
        std::string var_name = child[0]->get_name();
        if (!can_native_for(child, var_name)) {
            return nullptr;
        }

        llvm::Value* init = gen_i64_expr(child[0]->get_child()[0]);
        llvm::AllocaInst* slot = ensure_i64_slot(var_name);
        builder.CreateStore(init, slot);
        native_i64_vars[var_name].dirty = true;

        llvm::Value* step = child[2] != nullptr ? gen_i64_expr(child[2]) : builder.getInt64(1);
        llvm::Value* end = gen_i64_expr(child[1]);

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* step_zero_bb = llvm::BasicBlock::Create(ctx, "for.step.zero", fn);
        llvm::BasicBlock* step_ok_bb = llvm::BasicBlock::Create(ctx, "for.step.ok", fn);
        builder.CreateCondBr(builder.CreateICmpEQ(step, builder.getInt64(0)), step_zero_bb,
                             step_ok_bb);

        builder.SetInsertPoint(step_zero_bb);
        builder.CreateCall(get_rt("rt_for_step_check", builder.getVoidTy(), {ptr_ty}),
                           {box_i64(step)});
        builder.CreateBr(step_ok_bb);

        builder.SetInsertPoint(step_ok_bb);
        llvm::Value* iter_mark = frame_mark();
        llvm::BasicBlock* header = llvm::BasicBlock::Create(ctx, "for.i64.cond", fn);
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "for.i64.body", fn);
        llvm::BasicBlock* latch = llvm::BasicBlock::Create(ctx, "for.i64.latch", fn);
        llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "for.i64.exit", fn);
        builder.CreateBr(header);

        builder.SetInsertPoint(header);
        llvm::Value* current = builder.CreateLoad(i64_ty, slot);
        llvm::Value* step_positive = builder.CreateICmpSGT(step, builder.getInt64(0));
        llvm::Value* positive_cond = builder.CreateICmpSLE(current, end);
        llvm::Value* negative_cond = builder.CreateICmpSGE(current, end);
        builder.CreateCondBr(builder.CreateSelect(step_positive, positive_cond, negative_cond),
                             body_bb, exit_bb);

        builder.SetInsertPoint(body_bb);
        frame_push();
        for (size_t i = 3; i < child.size(); ++i) {
            gen_native_for_stmt(child[i]);
        }
        builder.CreateBr(latch);

        builder.SetInsertPoint(latch);
        llvm::Value* next = builder.CreateAdd(builder.CreateLoad(i64_ty, slot), step);
        builder.CreateStore(next, slot);
        native_i64_vars[var_name].dirty = true;
        frame_release(iter_mark);
        builder.CreateBr(header);

        builder.SetInsertPoint(exit_bb);
        frame_release(iter_mark);
        return make_none();
    }

    llvm::Value* gen_if(const std::shared_ptr<Node>& node) {
        flush_native_vars();
        clear_native_locals();
        bool saved_native_i64_enabled = native_i64_enabled;
        native_i64_enabled = false;
        IfNode* if_node = dynamic_cast<IfNode*>(node.get());
        llvm::Value* cond = gen(if_node->get_condition());
        if (cond == nullptr) {
            native_i64_enabled = saved_native_i64_enabled;
            return nullptr;
        }
        llvm::Value* taken = builder.CreateICmpNE(
            builder.CreateCall(get_rt("rt_cond_eq1", i64_ty, {ptr_ty}), {cond}),
            builder.getInt64(0));

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx, "if.then", fn);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx, "if.else", fn);
        llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "if.merge", fn);
        llvm::AllocaInst* slot = entry_alloca(ptr_ty);
        builder.CreateCondBr(taken, then_bb, else_bb);

        bool merge_reachable = false;
        builder.SetInsertPoint(then_bb);
        llvm::Value* then_value = gen_statements(if_node->get_expr());
        if (!errors.empty()) return nullptr;
        if (then_value != nullptr) {
            builder.CreateStore(then_value, slot);
        }
        if (!block_terminated()) {
            builder.CreateBr(merge_bb);
            merge_reachable = true;
        }

        builder.SetInsertPoint(else_bb);
        llvm::Value* else_value =
            if_node->get_else().empty() ? make_int(0) : gen_statements(if_node->get_else());
        if (!errors.empty()) return nullptr;
        if (else_value != nullptr) {
            builder.CreateStore(else_value, slot);
        }
        if (!block_terminated()) {
            builder.CreateBr(merge_bb);
            merge_reachable = true;
        }

        if (!merge_reachable) {
            merge_bb->eraseFromParent();
            native_i64_enabled = saved_native_i64_enabled;
            clear_native_locals();
            return nullptr;
        }
        builder.SetInsertPoint(merge_bb);
        llvm::Value* result = builder.CreateLoad(ptr_ty, slot);
        native_i64_enabled = saved_native_i64_enabled;
        clear_native_locals();
        return result;
    }

    llvm::Value* gen_while(const std::shared_ptr<Node>& node) {
        flush_native_vars();
        clear_native_locals();
        bool saved_native_i64_enabled = native_i64_enabled;
        native_i64_enabled = false;
        NodeList child = node->get_child();
        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* header = llvm::BasicBlock::Create(ctx, "while.cond", fn);
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "while.body", fn);
        llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "while.exit", fn);

        llvm::Value* mark = frame_mark();
        builder.CreateBr(header);

        // One arena frame per iteration covers the condition and the body.
        builder.SetInsertPoint(header);
        frame_push();
        llvm::Value* cond = gen(child[0]);
        if (cond == nullptr) return nullptr;
        llvm::Value* enter = builder.CreateICmpEQ(as_int(cond), builder.getInt64(1));
        builder.CreateCondBr(enter, body_bb, exit_bb);

        builder.SetInsertPoint(body_bb);
        loops.push_back({header, exit_bb, mark, true});
        bool terminated = gen_body(NodeList(child.begin() + 1, child.end()));
        loops.pop_back();
        if (!errors.empty()) return nullptr;
        if (!terminated) {
            frame_release(mark);
            builder.CreateBr(header);
        }

        builder.SetInsertPoint(exit_bb);
        frame_release(mark);
        llvm::Value* result = make_none();
        native_i64_enabled = saved_native_i64_enabled;
        clear_native_locals();
        return result;
    }

    llvm::Value* gen_repeat(const std::shared_ptr<Node>& node) {
        flush_native_vars();
        clear_native_locals();
        bool saved_native_i64_enabled = native_i64_enabled;
        native_i64_enabled = false;
        NodeList child = node->get_child();
        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "repeat.body", fn);
        llvm::BasicBlock* latch = llvm::BasicBlock::Create(ctx, "repeat.cond", fn);
        llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "repeat.exit", fn);

        llvm::Value* mark = frame_mark();
        builder.CreateBr(body_bb);

        builder.SetInsertPoint(body_bb);
        frame_push();
        loops.push_back({latch, exit_bb, mark, false});
        bool terminated = gen_body(NodeList(child.begin() + 1, child.end()));
        loops.pop_back();
        if (!errors.empty()) return nullptr;
        if (!terminated) {
            builder.CreateBr(latch);
        }

        builder.SetInsertPoint(latch);
        llvm::Value* cond = gen(child[0]);
        if (cond == nullptr) return nullptr;
        llvm::Value* again = builder.CreateICmpEQ(as_int(cond), builder.getInt64(0));
        frame_release(mark);
        builder.CreateCondBr(again, body_bb, exit_bb);

        builder.SetInsertPoint(exit_bb);
        frame_release(mark);
        llvm::Value* result = make_none();
        native_i64_enabled = saved_native_i64_enabled;
        clear_native_locals();
        return result;
    }

    llvm::Value* gen_for(const std::shared_ptr<Node>& node) {
        if (llvm::Value* native = gen_native_for(node)) {
            return native;
        }

        flush_native_vars();
        clear_native_locals();
        bool saved_native_i64_enabled = native_i64_enabled;
        native_i64_enabled = false;
        NodeList child = node->get_child();
        // Evaluation order matches visit_for: assign, step (default 1), end.
        llvm::Value* init = gen(child[0]);
        if (init == nullptr) return nullptr;
        llvm::Value* step = child[2] != nullptr ? gen(child[2]) : make_int(1);
        if (step == nullptr) return nullptr;
        llvm::Value* end = gen(child[1]);
        if (end == nullptr) return nullptr;
        builder.CreateCall(get_rt("rt_for_step_check", builder.getVoidTy(), {ptr_ty}), {step});

        std::string var_name = child[0]->get_name();
        llvm::AllocaInst* slot = entry_alloca(ptr_ty);
        builder.CreateStore(init, slot);

        // Dedicated frame keeping the latest loop-variable value alive even if
        // the body rebinds the variable (the interpreter holds it in a local).
        llvm::Value* var_frame = frame_mark();
        frame_push();
        llvm::Value* iter_mark = frame_mark();

        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* header = llvm::BasicBlock::Create(ctx, "for.cond", fn);
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx, "for.body", fn);
        llvm::BasicBlock* latch = llvm::BasicBlock::Create(ctx, "for.latch", fn);
        llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(ctx, "for.exit", fn);
        builder.CreateBr(header);

        builder.SetInsertPoint(header);
        llvm::Value* keep_going = builder.CreateICmpNE(
            builder.CreateCall(get_rt("rt_for_cond", i64_ty, {ptr_ty, ptr_ty, ptr_ty}),
                               {builder.CreateLoad(ptr_ty, slot), end, step}),
            builder.getInt64(0));
        builder.CreateCondBr(keep_going, body_bb, exit_bb);

        builder.SetInsertPoint(body_bb);
        frame_push();
        loops.push_back({latch, exit_bb, iter_mark, false});
        bool terminated = gen_body(NodeList(child.begin() + 3, child.end()));
        loops.pop_back();
        if (!errors.empty()) return nullptr;
        if (!terminated) {
            builder.CreateBr(latch);
        }

        // Latch mirrors visit_for: the next value comes from the value read at
        // the previous latch, not from body reassignments of the variable.
        builder.SetInsertPoint(latch);
        llvm::Value* next = builder.CreateCall(
            get_rt("rt_bin_op", ptr_ty, {i64_ty, ptr_ty, ptr_ty}),
            {builder.getInt64(RT_OP_ADD), builder.CreateLoad(ptr_ty, slot), step});
        builder.CreateCall(get_rt("rt_set_var", ptr_ty, {ptr_ty, ptr_ty}),
                           {cstring(var_name), next});
        builder.CreateCall(get_rt("rt_loop_keep", builder.getVoidTy(), {i64_ty, ptr_ty}),
                           {var_frame, next});
        builder.CreateStore(next, slot);
        frame_release(iter_mark);
        builder.CreateBr(header);

        builder.SetInsertPoint(exit_bb);
        frame_release(var_frame);
        llvm::Value* result = make_none();
        native_i64_enabled = saved_native_i64_enabled;
        clear_native_locals();
        return result;
    }

    // Emits a loop body (one or more statements). Returns true when the body
    // terminated the current block (break/continue/return on every path).
    bool gen_body(const NodeList& body) {
        for (const auto& stmt : body) {
            if (gen(stmt) == nullptr) {
                return block_terminated() || !errors.empty();
            }
        }
        return false;
    }

    llvm::Value* gen_loop_jump(const std::shared_ptr<Node>& node, bool is_break) {
        if (loops.empty()) {
            error(std::string("compile error: ") + (is_break ? "break" : "continue") +
                      " outside of a loop is not supported by the compiler",
                  node);
            return nullptr;
        }
        const LoopContext& loop = loops.back();
        if (is_break) {
            frame_release(loop.iter_mark);
            builder.CreateBr(loop.exit);
        } else {
            if (loop.release_on_continue) {
                frame_release(loop.iter_mark);
            }
            builder.CreateBr(loop.latch);
        }
        return nullptr;
    }

    llvm::Value* gen_return(const std::shared_ptr<Node>& node) {
        llvm::Value* value = gen(node->get_child()[0]);
        if (value == nullptr) return nullptr;
        llvm::Function* fn = builder.GetInsertBlock()->getParent();
        if (fn->getName() == "main") {
            // Top-level `return` does not stop execution in the interpreter.
            return value;
        }
        builder.CreateRet(value);
        return nullptr;
    }

    /// ---- functions and calls ----

    bool can_native_i64_algo_statement(const std::shared_ptr<Node>& stmt,
                                       const std::unordered_set<std::string>& vars,
                                       const std::string& self_name, size_t self_arity) {
        if (stmt->get_type() == NODE_RETURN) {
            NodeList child = stmt->get_child();
            return child.size() == 1 && can_i64_expr(child[0], &vars, self_name, self_arity);
        }
        if (stmt->get_type() == NODE_IF) {
            IfNode* if_node = dynamic_cast<IfNode*>(stmt.get());
            if (!can_i64_expr(if_node->get_condition(), &vars, self_name, self_arity)) {
                return false;
            }
            for (const auto& expr : if_node->get_expr()) {
                if (!can_native_i64_algo_statement(expr, vars, self_name, self_arity)) {
                    return false;
                }
            }
            for (const auto& expr : if_node->get_else()) {
                if (!can_native_i64_algo_statement(expr, vars, self_name, self_arity)) {
                    return false;
                }
            }
            return true;
        }
        return can_i64_expr(stmt, &vars, self_name, self_arity);
    }

    bool can_native_i64_algo(const std::shared_ptr<Node>& node, const std::string& name,
                             const std::vector<std::string>& arg_names) {
        if (arg_names.empty()) {
            return false;
        }
        AlgorithmDefNode* def = dynamic_cast<AlgorithmDefNode*>(node.get());
        if (!def) {
            return false;
        }
        std::unordered_set<std::string> vars(arg_names.begin(), arg_names.end());
        for (const auto& stmt : def->get_body()) {
            if (!can_native_i64_algo_statement(stmt, vars, name, arg_names.size())) {
                return false;
            }
        }
        return true;
    }

    void emit_native_i64_return(llvm::Value* value) {
        if (active_i64_memo_id && active_i64_memo_arg != nullptr) {
            builder.CreateCall(get_rt("rt_i64_memo_store", builder.getVoidTy(), {i64_ty, i64_ty, i64_ty}),
                               {builder.getInt64(*active_i64_memo_id), active_i64_memo_arg, value});
        }
        builder.CreateRet(value);
    }

    bool gen_native_i64_algo_block(const NodeList& body, llvm::Value*& last_value) {
        for (const auto& stmt : body) {
            if (stmt->get_type() == NODE_RETURN) {
                llvm::Value* value = gen_i64_expr(stmt->get_child()[0]);
                emit_native_i64_return(value);
                return true;
            }
            if (stmt->get_type() == NODE_IF) {
                IfNode* if_node = dynamic_cast<IfNode*>(stmt.get());
                llvm::Function* fn = builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx, "i64.if.then", fn);
                llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx, "i64.if.else", fn);
                llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "i64.if.merge", fn);
                llvm::Value* cond = builder.CreateICmpEQ(gen_i64_expr(if_node->get_condition()),
                                                         builder.getInt64(1));
                builder.CreateCondBr(cond, then_bb, else_bb);

                bool merge_reachable = false;
                builder.SetInsertPoint(then_bb);
                llvm::Value* then_last = nullptr;
                bool then_terminated = gen_native_i64_algo_block(if_node->get_expr(), then_last);
                if (!then_terminated && !block_terminated()) {
                    if (then_last != nullptr) {
                        last_value = then_last;
                    }
                    builder.CreateBr(merge_bb);
                    merge_reachable = true;
                }

                builder.SetInsertPoint(else_bb);
                llvm::Value* else_last = nullptr;
                bool else_terminated = gen_native_i64_algo_block(if_node->get_else(), else_last);
                if (!else_terminated && !block_terminated()) {
                    if (else_last != nullptr) {
                        last_value = else_last;
                    }
                    builder.CreateBr(merge_bb);
                    merge_reachable = true;
                }

                if (!merge_reachable) {
                    merge_bb->eraseFromParent();
                    return true;
                }
                builder.SetInsertPoint(merge_bb);
            } else {
                last_value = gen_i64_expr(stmt);
            }
            if (block_terminated()) {
                return true;
            }
        }
        return false;
    }

    llvm::Function* try_emit_native_i64_algo(const std::shared_ptr<Node>& node,
                                             const std::string& name,
                                             const std::vector<std::string>& arg_names) {
        bool memoizable = is_memoizable_numeric_algo(node, name, arg_names);
        if (memoizable && arg_names.size() != 1) {
            return nullptr;
        }
        if (!can_native_i64_algo(node, name, arg_names)) {
            return nullptr;
        }

        std::vector<llvm::Type*> arg_types(arg_names.size(), i64_ty);
        llvm::Function* fn = llvm::Function::Create(
            llvm::FunctionType::get(i64_ty, arg_types, false), llvm::Function::PrivateLinkage,
            "ps.i64." + std::to_string(algo_counter++) + "." + name, module);
        native_i64_algos[name] = NativeI64Algo{fn, arg_names.size()};

        llvm::IRBuilderBase::InsertPoint saved_ip = builder.saveIP();
        std::vector<LoopContext> saved_loops;
        saved_loops.swap(loops);
        auto saved_native_vars = std::move(native_i64_vars);
        auto saved_known_arrays = std::move(known_arrays);
        auto saved_numeric_arrays = std::move(numeric_arrays);
        bool saved_native_enabled = native_i64_enabled;
        std::optional<int64_t> saved_memo_id = active_i64_memo_id;
        llvm::Value* saved_memo_arg = active_i64_memo_arg;

        native_i64_vars.clear();
        known_arrays.clear();
        numeric_arrays.clear();
        native_i64_enabled = true;
        active_i64_memo_id.reset();
        active_i64_memo_arg = nullptr;

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
        builder.SetInsertPoint(entry);
        size_t index = 0;
        llvm::Value* first_arg = nullptr;
        for (llvm::Argument& arg : fn->args()) {
            if (index == 0) {
                first_arg = &arg;
            }
            llvm::AllocaInst* slot = entry_alloca(i64_ty);
            builder.CreateStore(&arg, slot);
            native_i64_vars[arg_names[index++]] = NativeI64Var{slot, false};
        }

        if (memoizable) {
            int64_t memo_id = native_i64_memo_counter++;
            llvm::AllocaInst* memo_out = entry_alloca(i64_ty);
            llvm::Value* hit = builder.CreateICmpNE(
                builder.CreateCall(get_rt("rt_i64_memo_lookup", i64_ty, {i64_ty, i64_ty, ptr_ty}),
                                   {builder.getInt64(memo_id), first_arg, memo_out}),
                builder.getInt64(0));
            llvm::BasicBlock* hit_bb = llvm::BasicBlock::Create(ctx, "i64.memo.hit", fn);
            llvm::BasicBlock* miss_bb = llvm::BasicBlock::Create(ctx, "i64.memo.miss", fn);
            builder.CreateCondBr(hit, hit_bb, miss_bb);

            builder.SetInsertPoint(hit_bb);
            builder.CreateRet(builder.CreateLoad(i64_ty, memo_out));

            builder.SetInsertPoint(miss_bb);
            active_i64_memo_id = memo_id;
            active_i64_memo_arg = first_arg;
        }

        AlgorithmDefNode* def = dynamic_cast<AlgorithmDefNode*>(node.get());
        llvm::Value* last = nullptr;
        bool terminated = gen_native_i64_algo_block(def->get_body(), last);
        if (!terminated && !block_terminated()) {
            emit_native_i64_return(last != nullptr ? last : builder.getInt64(0));
        }

        native_i64_vars = std::move(saved_native_vars);
        known_arrays = std::move(saved_known_arrays);
        numeric_arrays = std::move(saved_numeric_arrays);
        native_i64_enabled = saved_native_enabled;
        active_i64_memo_id = saved_memo_id;
        active_i64_memo_arg = saved_memo_arg;
        loops.swap(saved_loops);
        builder.restoreIP(saved_ip);
        return fn;
    }

    llvm::Function* emit_algo_function(const std::shared_ptr<Node>& node, const std::string& name) {
        AlgorithmDefNode* def = dynamic_cast<AlgorithmDefNode*>(node.get());
        llvm::Function* fn = llvm::Function::Create(
            llvm::FunctionType::get(ptr_ty, false), llvm::Function::PrivateLinkage,
            "ps.algo." + std::to_string(algo_counter++) + "." + name, module);

        llvm::IRBuilderBase::InsertPoint saved = builder.saveIP();
        std::vector<LoopContext> saved_loops;
        saved_loops.swap(loops);

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
        builder.SetInsertPoint(entry);
        llvm::Value* last = gen_statements(def->get_body());
        if (!block_terminated() && errors.empty()) {
            builder.CreateRet(last != nullptr ? last : make_none());
        }

        loops.swap(saved_loops);
        builder.restoreIP(saved);
        if (!errors.empty()) {
            return nullptr;
        }
        return fn;
    }

    llvm::Value* gen_algo_value(const std::shared_ptr<Node>& node, const std::string& name,
                                bool define_global) {
        std::vector<std::string> arg_names;
        for (const auto& tok : node->get_toks()) {
            arg_names.push_back(tok->get_value());
        }

        try_emit_native_i64_algo(node, name, arg_names);

        llvm::Function* fn = emit_algo_function(node, name);
        if (fn == nullptr) {
            return nullptr;
        }

        bool memoizable = is_memoizable_numeric_algo(node, name, arg_names);
        std::string rt_name = define_global ? "rt_define_algo" : "rt_make_algo";
        return builder.CreateCall(get_rt(rt_name, ptr_ty, {ptr_ty, ptr_ty, ptr_ty, i64_ty, i64_ty}),
                                  {cstring(name), fn, string_array(arg_names, "args"),
                                   builder.getInt64(static_cast<int64_t>(arg_names.size())),
                                   builder.getInt64(memoizable ? 1 : 0)});
    }

    llvm::Value* gen_algo_def(const std::shared_ptr<Node>& node) {
        std::string name = node->get_name();
        size_t scope = name.find("::");
        if (scope == std::string::npos) {
            // Registration happens at the definition site, like visit_algo_def.
            return gen_algo_value(node, name, true);
        }

        std::string struct_name = name.substr(0, scope);
        std::string method_name = name.substr(scope + 2);
        llvm::Value* method = gen_algo_value(node, name, true);
        if (method == nullptr) {
            return nullptr;
        }
        builder.CreateCall(get_rt("rt_struct_add_method", ptr_ty, {ptr_ty, ptr_ty, ptr_ty}),
                           {cstring(struct_name), cstring(method_name), method});
        return method;
    }

    llvm::Value* gen_algo_call(const std::shared_ptr<Node>& node) {
        if (can_i64_expr(node)) {
            return box_i64(gen_i64_expr(node));
        }

        std::string array_name, method_name;
        NodeList direct_args;
        if (is_direct_array_method_call(node, array_name, method_name, direct_args) &&
            (method_name == "push" || method_name == "push_back") && direct_args.size() == 1 &&
            can_i64_expr(direct_args[0])) {
            llvm::Value* raw_value = gen_i64_expr(direct_args[0]);
            llvm::Value* value = builder.CreateCall(
                get_rt("rt_array_push_i64", ptr_ty, {ptr_ty, i64_ty}),
                {gen_direct_array_var(array_name), raw_value});
            if (numeric_arrays.count(array_name) != 0) {
                numeric_arrays.insert(array_name);
            }
            return value;
        }

        if (is_direct_array_method_call(node, array_name, method_name, direct_args)) {
            if ((method_name == "push" || method_name == "push_back" ||
                 method_name == "insert") &&
                !direct_args.empty() && !can_i64_expr(direct_args.back())) {
                numeric_arrays.erase(array_name);
            }
        }

        flush_native_vars();
        AlgorithmCallNode* call = dynamic_cast<AlgorithmCallNode*>(node.get());
        std::shared_ptr<Node> callee_node = call->get_call();
        llvm::Value* callee;
        if (callee_node->get_type() == NODE_VARACCESS) {
            callee = gen_var_access(callee_node);
        } else {
            callee = gen(callee_node);
        }
        if (callee == nullptr) return nullptr;

        const NodeList& args = call->get_args();
        llvm::Value* argv;
        if (args.empty()) {
            argv = llvm::ConstantPointerNull::get(ptr_ty);
        } else {
            llvm::ArrayType* argv_ty = llvm::ArrayType::get(ptr_ty, args.size());
            llvm::Function* fn = builder.GetInsertBlock()->getParent();
            llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
            llvm::AllocaInst* slots = tmp.CreateAlloca(argv_ty);
            for (size_t i = 0; i < args.size(); ++i) {
                llvm::Value* arg = gen(args[i]);
                if (arg == nullptr) return nullptr;
                builder.CreateStore(arg, builder.CreateConstInBoundsGEP2_64(argv_ty, slots, 0, i));
            }
            argv = slots;
        }
        return builder.CreateCall(
            get_rt("rt_call", ptr_ty, {ptr_ty, ptr_ty, i64_ty}),
            {callee, argv, builder.getInt64(static_cast<int64_t>(args.size()))});
    }

    llvm::Value* gen_struct_def(const std::shared_ptr<Node>& node) {
        std::vector<std::string> member_names;
        for (const auto& tok : node->get_toks()) {
            member_names.push_back(tok->get_value());
        }

        std::vector<std::string> method_names;
        std::vector<llvm::Value*> methods;
        for (const auto& method_node : node->get_child()) {
            std::string method_name = method_node->get_name();
            llvm::Value* method = gen_algo_value(method_node, method_name, false);
            if (method == nullptr) {
                return nullptr;
            }
            method_names.push_back(method_name);
            methods.push_back(method);
        }

        return builder.CreateCall(
            get_rt("rt_define_struct", ptr_ty, {ptr_ty, ptr_ty, i64_ty, ptr_ty, ptr_ty, i64_ty}),
            {cstring(node->get_name()), string_array(member_names, "members"),
             builder.getInt64(static_cast<int64_t>(member_names.size())),
             string_array(method_names, "method.names"), value_array(methods),
             builder.getInt64(static_cast<int64_t>(methods.size()))});
    }

    /// ---- arrays, hash tables, members ----

    llvm::Value* gen_array(const std::shared_ptr<Node>& node) {
        llvm::Value* array = builder.CreateCall(get_rt("rt_array_new", ptr_ty, {}));
        for (const auto& element : node->get_child()) {
            llvm::Value* value = gen(element);
            if (value == nullptr) return nullptr;
            builder.CreateCall(get_rt("rt_array_push", builder.getVoidTy(), {ptr_ty, ptr_ty}),
                               {array, value});
        }
        return array;
    }

    llvm::Value* gen_array_access(const std::shared_ptr<Node>& node) {
        NodeList child = node->get_child();
        llvm::Value* container = gen(child[0]);
        if (container == nullptr) return nullptr;
        llvm::Value* index = gen(child[1]);
        if (index == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_index", ptr_ty, {ptr_ty, ptr_ty}), {container, index});
    }

    llvm::Value* gen_array_assign(const std::shared_ptr<Node>& node) {
        NodeList child = node->get_child();
        if (child[0]->get_type() == NODE_MEMACCESS) {
            NodeList member_child = child[0]->get_child();
            llvm::Value* object = gen(member_child[0]);
            if (object == nullptr) return nullptr;
            llvm::Value* value = gen(child[1]);
            if (value == nullptr) return nullptr;
            return builder.CreateCall(get_rt("rt_member_assign", ptr_ty, {ptr_ty, ptr_ty, ptr_ty}),
                                      {object, cstring(member_child[1]->get_name()), value});
        }
        if (child[0]->get_type() != NODE_ARRACCESS) {
            error(
                "compile error: assignment target must be an array element or "
                "object member",
                node);
            return nullptr;
        }
        NodeList access_child = child[0]->get_child();
        llvm::Value* container = gen(access_child[0]);
        if (container == nullptr) return nullptr;
        llvm::Value* index = gen(access_child[1]);
        if (index == nullptr) return nullptr;
        llvm::Value* value = gen(child[1]);
        if (value == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_index_assign", ptr_ty, {ptr_ty, ptr_ty, ptr_ty}),
                                  {container, index, value});
    }

    llvm::Value* gen_member_access(const std::shared_ptr<Node>& node) {
        NodeList child = node->get_child();
        llvm::Value* object = gen(child[0]);
        if (object == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_member_access", ptr_ty, {ptr_ty, ptr_ty}),
                                  {object, cstring(child[1]->get_name())});
    }
};

}  // namespace

bool Compiler::compile(const NodeList& ast, llvm::Module& module,
                       std::vector<std::string>& errors) {
    CodeGen codegen(module, errors);
    return codegen.run(ast);
}

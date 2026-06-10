/// --------------------
/// LLVM AOT compiler
/// --------------------
///
/// Lowers the AST to LLVM IR. Every language operation becomes a call into
/// the runtime (src/runtime.h); control flow becomes real basic blocks. The
/// emitted code mirrors Interpreter::visit_* semantics; see
/// docs/superpowers/specs/2026-06-10-llvm-compiler-design.md.

#include "compiler.h"
#include "node.h"
#include "runtime.h"
#include "token.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <string>
#include <vector>

namespace {

class CodeGen {
public:
    CodeGen(llvm::Module &_module, std::vector<std::string> &_errors)
        : module(_module), ctx(_module.getContext()), builder(ctx),
          errors(_errors) {
        ptr_ty = builder.getPtrTy();
        i64_ty = builder.getInt64Ty();
        f64_ty = builder.getDoubleTy();
    }

    bool run(const NodeList &ast) {
        llvm::Function *main_fn = llvm::Function::Create(
            llvm::FunctionType::get(builder.getInt32Ty(), false),
            llvm::Function::ExternalLinkage, "main", module);
        llvm::BasicBlock *entry = llvm::BasicBlock::Create(ctx, "entry", main_fn);
        builder.SetInsertPoint(entry);
        builder.CreateCall(get_rt("rt_init", builder.getVoidTy(), {}));

        for (const auto &node : ast) {
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
        llvm::BasicBlock *latch;
        llvm::BasicBlock *exit;
        llvm::Value *iter_mark;
        // `while` re-pushes its iteration frame in the header, so `continue`
        // must release first; `for`/`repeat` latches release it themselves.
        bool release_on_continue;
    };

    llvm::Module &module;
    llvm::LLVMContext &ctx;
    llvm::IRBuilder<> builder;
    std::vector<std::string> &errors;

    llvm::PointerType *ptr_ty{nullptr};
    llvm::Type *i64_ty{nullptr};
    llvm::Type *f64_ty{nullptr};

    std::vector<LoopContext> loops;
    std::map<std::string, llvm::Value *> string_constants;
    int algo_counter{0};

    /// ---- helpers ----

    llvm::FunctionCallee get_rt(const std::string &name, llvm::Type *ret,
                                std::vector<llvm::Type *> args) {
        return module.getOrInsertFunction(
            name, llvm::FunctionType::get(ret, args, false));
    }

    bool block_terminated() {
        return builder.GetInsertBlock()->getTerminator() != nullptr;
    }

    llvm::Value *cstring(const std::string &text) {
        auto found = string_constants.find(text);
        if (found != string_constants.end()) {
            return found->second;
        }
        llvm::Value *global = builder.CreateGlobalString(text, "str");
        string_constants[text] = global;
        return global;
    }

    llvm::AllocaInst *entry_alloca(llvm::Type *type) {
        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return tmp.CreateAlloca(type);
    }

    void error(const std::string &message, const std::shared_ptr<Node> &node) {
        std::string suffix;
        std::shared_ptr<Token> tok = node ? node->get_tok() : nullptr;
        if (tok) {
            suffix = " (line " + std::to_string(tok->get_pos().line + 1) + ")";
        }
        errors.push_back(message + suffix);
    }

    llvm::Value *make_none() {
        return builder.CreateCall(get_rt("rt_make_none", ptr_ty, {}));
    }

    llvm::Value *make_int(int64_t v) {
        return builder.CreateCall(get_rt("rt_make_int", ptr_ty, {i64_ty}),
                                  {builder.getInt64(v)});
    }

    llvm::Value *frame_mark() {
        return builder.CreateCall(get_rt("rt_frame_mark", i64_ty, {}));
    }

    void frame_push() {
        builder.CreateCall(get_rt("rt_frame_push", builder.getVoidTy(), {}));
    }

    void frame_release(llvm::Value *mark) {
        builder.CreateCall(get_rt("rt_frame_release", builder.getVoidTy(), {i64_ty}),
                           {mark});
    }

    /// ---- dispatch ----

    // Emits code for one node. Returns the node's value (an LLVM `ptr` to a
    // runtime Value) or nullptr when the node terminated the current block
    // (break/continue/return) or reported a compile error.
    llvm::Value *gen(const std::shared_ptr<Node> &node) {
        const std::string type = node->get_type();
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
        if (type == NODE_STRUCTDEF) {
            error("compile error: Struct is not supported by the compiler yet",
                  node);
            return nullptr;
        }
        error("compile error: unsupported statement " + type, node);
        return nullptr;
    }

    // Emits a statement sequence; returns the last statement's value, or
    // nullptr when the sequence terminated the block (or errored).
    llvm::Value *gen_statements(const NodeList &nodes) {
        llvm::Value *last = nullptr;
        for (const auto &node : nodes) {
            last = gen(node);
            if (last == nullptr) {
                return nullptr;
            }
        }
        return last;
    }

    /// ---- expressions ----

    llvm::Value *gen_literal(const std::shared_ptr<Node> &node) {
        std::shared_ptr<Token> tok = node->get_tok();
        if (tok->get_type() == TOKEN_INT) {
            return make_int(std::stoll(tok->get_value()));
        }
        if (tok->get_type() == TOKEN_FLOAT) {
            return builder.CreateCall(
                get_rt("rt_make_float", ptr_ty, {f64_ty}),
                {llvm::ConstantFP::get(f64_ty, std::stod(tok->get_value()))});
        }
        if (tok->get_type() == TOKEN_STRING) {
            return builder.CreateCall(get_rt("rt_make_string", ptr_ty, {ptr_ty}),
                                      {cstring(tok->get_value())});
        }
        error("compile error: unsupported literal " + tok->get_type(), node);
        return nullptr;
    }

    llvm::Value *gen_var_access(const std::shared_ptr<Node> &node) {
        return builder.CreateCall(get_rt("rt_get_var", ptr_ty, {ptr_ty}),
                                  {cstring(node->get_name())});
    }

    llvm::Value *gen_var_assign(const std::shared_ptr<Node> &node) {
        llvm::Value *value = gen(node->get_child()[0]);
        if (value == nullptr) {
            return nullptr;
        }
        return builder.CreateCall(get_rt("rt_set_var", ptr_ty, {ptr_ty, ptr_ty}),
                                  {cstring(node->get_name()), value});
    }

    llvm::Value *as_int(llvm::Value *value) {
        return builder.CreateCall(get_rt("rt_as_int", i64_ty, {ptr_ty}), {value});
    }

    llvm::Value *gen_bin_op(const std::shared_ptr<Node> &node) {
        std::shared_ptr<Token> op = node->get_tok();
        if (op->get_type() == TOKEN_KEYWORD &&
            (op->get_value() == "and" || op->get_value() == "or")) {
            return gen_short_circuit(node, op->get_value() == "and");
        }

        static const std::map<std::string, int64_t> OP_CODES{
            {TOKEN_ADD, RT_OP_ADD},     {TOKEN_SUB, RT_OP_SUB},
            {TOKEN_MUL, RT_OP_MUL},     {TOKEN_DIV, RT_OP_DIV},
            {TOKEN_MOD, RT_OP_MOD},     {TOKEN_POW, RT_OP_POW},
            {TOKEN_EQUAL, RT_OP_EQUAL}, {TOKEN_NEQ, RT_OP_NEQ},
            {TOKEN_LESS, RT_OP_LESS},   {TOKEN_GREATER, RT_OP_GREATER},
            {TOKEN_LEQ, RT_OP_LEQ},     {TOKEN_GEQ, RT_OP_GEQ},
        };
        auto code = OP_CODES.find(op->get_type());
        if (code == OP_CODES.end()) {
            error("compile error: unsupported operator " + op->get_type(), node);
            return nullptr;
        }

        NodeList child = node->get_child();
        llvm::Value *lhs = gen(child[0]);
        if (lhs == nullptr) return nullptr;
        llvm::Value *rhs = gen(child[1]);
        if (rhs == nullptr) return nullptr;
        return builder.CreateCall(
            get_rt("rt_bin_op", ptr_ty, {i64_ty, ptr_ty, ptr_ty}),
            {builder.getInt64(code->second), lhs, rhs});
    }

    // Mirrors visit_bin_op's short-circuit `and` / `or`: the result is always
    // an Int 0/1 derived from as_int().
    llvm::Value *gen_short_circuit(const std::shared_ptr<Node> &node,
                                   bool is_and) {
        NodeList child = node->get_child();
        llvm::Value *lhs = gen(child[0]);
        if (lhs == nullptr) return nullptr;

        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *rhs_bb = llvm::BasicBlock::Create(ctx, "sc.rhs", fn);
        llvm::BasicBlock *short_bb = llvm::BasicBlock::Create(ctx, "sc.short", fn);
        llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(ctx, "sc.merge", fn);

        llvm::Value *lhs_true =
            builder.CreateICmpNE(as_int(lhs), builder.getInt64(0));
        if (is_and) {
            builder.CreateCondBr(lhs_true, rhs_bb, short_bb);
        } else {
            builder.CreateCondBr(lhs_true, short_bb, rhs_bb);
        }

        builder.SetInsertPoint(short_bb);
        llvm::Value *short_value = make_int(is_and ? 0 : 1);
        builder.CreateBr(merge_bb);

        builder.SetInsertPoint(rhs_bb);
        llvm::Value *rhs = gen(child[1]);
        if (rhs == nullptr) return nullptr;
        llvm::Value *rhs_value =
            builder.CreateCall(get_rt("rt_bool", ptr_ty, {ptr_ty}), {rhs});
        llvm::BasicBlock *rhs_end = builder.GetInsertBlock();
        builder.CreateBr(merge_bb);

        builder.SetInsertPoint(merge_bb);
        llvm::PHINode *phi = builder.CreatePHI(ptr_ty, 2);
        phi->addIncoming(short_value, short_bb);
        phi->addIncoming(rhs_value, rhs_end);
        return phi;
    }

    llvm::Value *gen_unary_op(const std::shared_ptr<Node> &node) {
        std::shared_ptr<Token> op = node->get_tok();
        int64_t code;
        if (op->get_type() == TOKEN_ADD) {
            code = RT_OP_UPLUS;
        } else if (op->get_type() == TOKEN_SUB) {
            code = RT_OP_UNEG;
        } else if (op->get_type() == TOKEN_KEYWORD && op->get_value() == "not") {
            code = RT_OP_UNOT;
        } else {
            error("compile error: unsupported unary operator " + op->get_type(),
                  node);
            return nullptr;
        }
        llvm::Value *operand = gen(node->get_child()[0]);
        if (operand == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_unary_op", ptr_ty, {i64_ty, ptr_ty}),
                                  {builder.getInt64(code), operand});
    }

    /// ---- control flow ----

    llvm::Value *gen_if(const std::shared_ptr<Node> &node) {
        IfNode *if_node = dynamic_cast<IfNode *>(node.get());
        llvm::Value *cond = gen(if_node->get_condition());
        if (cond == nullptr) return nullptr;
        llvm::Value *taken = builder.CreateICmpNE(
            builder.CreateCall(get_rt("rt_cond_eq1", i64_ty, {ptr_ty}), {cond}),
            builder.getInt64(0));

        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(ctx, "if.then", fn);
        llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(ctx, "if.else", fn);
        llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(ctx, "if.merge", fn);
        llvm::AllocaInst *slot = entry_alloca(ptr_ty);
        builder.CreateCondBr(taken, then_bb, else_bb);

        bool merge_reachable = false;
        builder.SetInsertPoint(then_bb);
        llvm::Value *then_value = gen_statements(if_node->get_expr());
        if (!errors.empty()) return nullptr;
        if (then_value != nullptr) {
            builder.CreateStore(then_value, slot);
        }
        if (!block_terminated()) {
            builder.CreateBr(merge_bb);
            merge_reachable = true;
        }

        builder.SetInsertPoint(else_bb);
        llvm::Value *else_value = if_node->get_else().empty()
                                      ? make_int(0)
                                      : gen_statements(if_node->get_else());
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
            return nullptr;
        }
        builder.SetInsertPoint(merge_bb);
        return builder.CreateLoad(ptr_ty, slot);
    }

    llvm::Value *gen_while(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *header = llvm::BasicBlock::Create(ctx, "while.cond", fn);
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(ctx, "while.body", fn);
        llvm::BasicBlock *exit_bb = llvm::BasicBlock::Create(ctx, "while.exit", fn);

        llvm::Value *mark = frame_mark();
        builder.CreateBr(header);

        // One arena frame per iteration covers the condition and the body.
        builder.SetInsertPoint(header);
        frame_push();
        llvm::Value *cond = gen(child[0]);
        if (cond == nullptr) return nullptr;
        llvm::Value *enter = builder.CreateICmpEQ(as_int(cond), builder.getInt64(1));
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
        return make_none();
    }

    llvm::Value *gen_repeat(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(ctx, "repeat.body", fn);
        llvm::BasicBlock *latch = llvm::BasicBlock::Create(ctx, "repeat.cond", fn);
        llvm::BasicBlock *exit_bb = llvm::BasicBlock::Create(ctx, "repeat.exit", fn);

        llvm::Value *mark = frame_mark();
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
        llvm::Value *cond = gen(child[0]);
        if (cond == nullptr) return nullptr;
        llvm::Value *again = builder.CreateICmpEQ(as_int(cond), builder.getInt64(0));
        frame_release(mark);
        builder.CreateCondBr(again, body_bb, exit_bb);

        builder.SetInsertPoint(exit_bb);
        frame_release(mark);
        return make_none();
    }

    llvm::Value *gen_for(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        // Evaluation order matches visit_for: assign, step (default 1), end.
        llvm::Value *init = gen(child[0]);
        if (init == nullptr) return nullptr;
        llvm::Value *step = child[2] != nullptr ? gen(child[2]) : make_int(1);
        if (step == nullptr) return nullptr;
        llvm::Value *end = gen(child[1]);
        if (end == nullptr) return nullptr;
        builder.CreateCall(get_rt("rt_for_step_check", builder.getVoidTy(),
                                  {ptr_ty}),
                           {step});

        const std::string var_name = child[0]->get_name();
        llvm::AllocaInst *slot = entry_alloca(ptr_ty);
        builder.CreateStore(init, slot);

        // Dedicated frame keeping the latest loop-variable value alive even if
        // the body rebinds the variable (the interpreter holds it in a local).
        llvm::Value *var_frame = frame_mark();
        frame_push();
        llvm::Value *iter_mark = frame_mark();

        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock *header = llvm::BasicBlock::Create(ctx, "for.cond", fn);
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(ctx, "for.body", fn);
        llvm::BasicBlock *latch = llvm::BasicBlock::Create(ctx, "for.latch", fn);
        llvm::BasicBlock *exit_bb = llvm::BasicBlock::Create(ctx, "for.exit", fn);
        builder.CreateBr(header);

        builder.SetInsertPoint(header);
        llvm::Value *keep_going = builder.CreateICmpNE(
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
        llvm::Value *next = builder.CreateCall(
            get_rt("rt_bin_op", ptr_ty, {i64_ty, ptr_ty, ptr_ty}),
            {builder.getInt64(RT_OP_ADD), builder.CreateLoad(ptr_ty, slot), step});
        builder.CreateCall(get_rt("rt_set_var", ptr_ty, {ptr_ty, ptr_ty}),
                           {cstring(var_name), next});
        builder.CreateCall(get_rt("rt_loop_keep", builder.getVoidTy(),
                                  {i64_ty, ptr_ty}),
                           {var_frame, next});
        builder.CreateStore(next, slot);
        frame_release(iter_mark);
        builder.CreateBr(header);

        builder.SetInsertPoint(exit_bb);
        frame_release(var_frame);
        return make_none();
    }

    // Emits a loop body (one or more statements). Returns true when the body
    // terminated the current block (break/continue/return on every path).
    bool gen_body(const NodeList &body) {
        for (const auto &stmt : body) {
            if (gen(stmt) == nullptr) {
                return block_terminated() || !errors.empty();
            }
        }
        return false;
    }

    llvm::Value *gen_loop_jump(const std::shared_ptr<Node> &node, bool is_break) {
        if (loops.empty()) {
            error(std::string("compile error: ") +
                      (is_break ? "break" : "continue") +
                      " outside of a loop is not supported by the compiler",
                  node);
            return nullptr;
        }
        const LoopContext &loop = loops.back();
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

    llvm::Value *gen_return(const std::shared_ptr<Node> &node) {
        llvm::Value *value = gen(node->get_child()[0]);
        if (value == nullptr) return nullptr;
        llvm::Function *fn = builder.GetInsertBlock()->getParent();
        if (fn->getName() == "main") {
            // Top-level `return` does not stop execution in the interpreter.
            return value;
        }
        builder.CreateRet(value);
        return nullptr;
    }

    /// ---- functions and calls ----

    llvm::Value *gen_algo_def(const std::shared_ptr<Node> &node) {
        const std::string name = node->get_name();
        if (name.find("::") != std::string::npos) {
            error("compile error: Struct is not supported by the compiler yet",
                  node);
            return nullptr;
        }

        AlgorithmDefNode *def = dynamic_cast<AlgorithmDefNode *>(node.get());
        std::vector<std::string> arg_names;
        for (const auto &tok : node->get_toks()) {
            arg_names.push_back(tok->get_value());
        }

        llvm::Function *fn = llvm::Function::Create(
            llvm::FunctionType::get(ptr_ty, false), llvm::Function::PrivateLinkage,
            "ps.algo." + std::to_string(algo_counter++) + "." + name, module);

        llvm::IRBuilderBase::InsertPoint saved = builder.saveIP();
        std::vector<LoopContext> saved_loops;
        saved_loops.swap(loops);

        llvm::BasicBlock *entry = llvm::BasicBlock::Create(ctx, "entry", fn);
        builder.SetInsertPoint(entry);
        llvm::Value *last = gen_statements(def->get_body());
        if (!block_terminated() && errors.empty()) {
            builder.CreateRet(last != nullptr ? last : make_none());
        }

        loops.swap(saved_loops);
        builder.restoreIP(saved);
        if (!errors.empty()) {
            return nullptr;
        }

        // Registration happens at the definition site, like visit_algo_def.
        llvm::ArrayType *names_ty = llvm::ArrayType::get(ptr_ty, arg_names.size());
        std::vector<llvm::Constant *> name_ptrs;
        for (const auto &arg : arg_names) {
            name_ptrs.push_back(llvm::cast<llvm::Constant>(cstring(arg)));
        }
        auto *names_global = new llvm::GlobalVariable(
            module, names_ty, true, llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(names_ty, name_ptrs), "args");
        return builder.CreateCall(
            get_rt("rt_define_algo", ptr_ty, {ptr_ty, ptr_ty, ptr_ty, i64_ty}),
            {cstring(name), fn, names_global,
             builder.getInt64(static_cast<int64_t>(arg_names.size()))});
    }

    llvm::Value *gen_algo_call(const std::shared_ptr<Node> &node) {
        AlgorithmCallNode *call = dynamic_cast<AlgorithmCallNode *>(node.get());
        std::shared_ptr<Node> callee_node = call->get_call();
        llvm::Value *callee;
        if (callee_node->get_type() == NODE_VARACCESS) {
            callee = gen_var_access(callee_node);
        } else {
            callee = gen(callee_node);
        }
        if (callee == nullptr) return nullptr;

        const NodeList &args = call->get_args();
        llvm::Value *argv;
        if (args.empty()) {
            argv = llvm::ConstantPointerNull::get(ptr_ty);
        } else {
            llvm::ArrayType *argv_ty = llvm::ArrayType::get(ptr_ty, args.size());
            llvm::Function *fn = builder.GetInsertBlock()->getParent();
            llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
            llvm::AllocaInst *slots = tmp.CreateAlloca(argv_ty);
            for (size_t i = 0; i < args.size(); ++i) {
                llvm::Value *arg = gen(args[i]);
                if (arg == nullptr) return nullptr;
                builder.CreateStore(
                    arg, builder.CreateConstInBoundsGEP2_64(argv_ty, slots, 0, i));
            }
            argv = slots;
        }
        return builder.CreateCall(
            get_rt("rt_call", ptr_ty, {ptr_ty, ptr_ty, i64_ty}),
            {callee, argv, builder.getInt64(static_cast<int64_t>(args.size()))});
    }

    /// ---- arrays, hash tables, members ----

    llvm::Value *gen_array(const std::shared_ptr<Node> &node) {
        llvm::Value *array = builder.CreateCall(get_rt("rt_array_new", ptr_ty, {}));
        for (const auto &element : node->get_child()) {
            llvm::Value *value = gen(element);
            if (value == nullptr) return nullptr;
            builder.CreateCall(get_rt("rt_array_push", builder.getVoidTy(),
                                      {ptr_ty, ptr_ty}),
                               {array, value});
        }
        return array;
    }

    llvm::Value *gen_array_access(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        llvm::Value *container = gen(child[0]);
        if (container == nullptr) return nullptr;
        llvm::Value *index = gen(child[1]);
        if (index == nullptr) return nullptr;
        return builder.CreateCall(get_rt("rt_index", ptr_ty, {ptr_ty, ptr_ty}),
                                  {container, index});
    }

    llvm::Value *gen_array_assign(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        if (child[0]->get_type() == NODE_MEMACCESS) {
            NodeList member_child = child[0]->get_child();
            llvm::Value *object = gen(member_child[0]);
            if (object == nullptr) return nullptr;
            llvm::Value *value = gen(child[1]);
            if (value == nullptr) return nullptr;
            return builder.CreateCall(
                get_rt("rt_member_assign", ptr_ty, {ptr_ty, ptr_ty, ptr_ty}),
                {object, cstring(member_child[1]->get_name()), value});
        }
        if (child[0]->get_type() != NODE_ARRACCESS) {
            error("compile error: assignment target must be an array element or "
                  "object member",
                  node);
            return nullptr;
        }
        NodeList access_child = child[0]->get_child();
        llvm::Value *container = gen(access_child[0]);
        if (container == nullptr) return nullptr;
        llvm::Value *index = gen(access_child[1]);
        if (index == nullptr) return nullptr;
        llvm::Value *value = gen(child[1]);
        if (value == nullptr) return nullptr;
        return builder.CreateCall(
            get_rt("rt_index_assign", ptr_ty, {ptr_ty, ptr_ty, ptr_ty}),
            {container, index, value});
    }

    llvm::Value *gen_member_access(const std::shared_ptr<Node> &node) {
        NodeList child = node->get_child();
        llvm::Value *object = gen(child[0]);
        if (object == nullptr) return nullptr;
        return builder.CreateCall(
            get_rt("rt_member_access", ptr_ty, {ptr_ty, ptr_ty}),
            {object, cstring(child[1]->get_name())});
    }
};

} // namespace

bool Compiler::compile(const NodeList &ast, llvm::Module &module,
                       std::vector<std::string> &errors) {
    CodeGen codegen(module, errors);
    return codegen.run(ast);
}

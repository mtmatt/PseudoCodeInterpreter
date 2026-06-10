# LLVM AOT Compiler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `pseudoc` binary that compiles `.ps` programs to native executables via LLVM, with stdout behavior identical to the interpreter.

**Architecture:** Codegen walks the existing AST and emits LLVM IR where every operation is a call into an `extern "C"` runtime (`rt_*`) layered on the existing `Value`/`SymbolTable` classes; the emitted object file is linked against `build/libpseudort.a`. See `docs/superpowers/specs/2026-06-10-llvm-compiler-design.md`.

**Tech Stack:** C++17 project code, LLVM 21 C++ API (Homebrew, `llvm-config`), GNU make, bash differential tests.

---

### Task 1: Differential test harness (failing first)

**Files:**
- Create: `test/compiler_tests.sh` (executable)
- Create: `test/compiler/basics.ps`, `test/compiler/control_flow.ps`, `test/compiler/functions.ps`, `test/compiler/collections.ps`, `test/compiler/errors.ps`
- Modify: `Makefile` (add `test-compiler` target)

- [x] **Step 1:** Write `test/compiler_tests.sh`: for each `.ps` in `test/compiler/` plus `test/test_fib.ps`, `test/test_repeat.ps`, `test/test_array_methods.ps`, `test/test_string_index.ps`, run `./pseudo f.ps > expected`, `./pseudoc f.ps -o tmpbin && ./tmpbin > actual`, diff. Exit nonzero on any mismatch. Also assert `./pseudoc test/test_struct.ps` fails with a struct compile error.
- [x] **Step 2:** Write the five new `.ps` fixtures covering: int/float/string arithmetic and comparisons, `and/or/not`, unary minus, `^` and `%`; if/else-if chains, for with step (incl. negative), while/repeat with break/continue, nested loops; recursion (fib), functions using caller scope, too-few-args error; arrays (literal, index assign, push/pop/insert/remove/resize/size/back), hash tables (set/get/contains/keys/size), string indexing and concatenation; index-out-of-range error.
- [x] **Step 3:** Run `bash test/compiler_tests.sh` — must FAIL (`pseudoc` missing). Commit.

### Task 2: Minimal intrusive changes to interpreter core

**Files:**
- Modify: `src/value.h` (Value gains `enable_shared_from_this`; add `PrecomputedNode`)
- Modify: `src/node.h` (add `NODE_PRECOMPUTED` constant)
- Modify: `src/interpreter.cpp` (`Interpreter::visit` dispatch case returning the precomputed value)
- Create: `src/imports.h`
- Modify: `src/pseudo.cpp` (move `ImportState`/`expand_imports` out of anonymous namespace, include `imports.h`)

- [x] **Step 1:** `class Value : public std::enable_shared_from_this<Value>`; add

```cpp
class PrecomputedNode : public Node {
public:
    explicit PrecomputedNode(std::shared_ptr<Value> _value) : value(_value) {}
    std::string get_node() override { return "PRECOMPUTED"; }
    std::string get_type() override { return NODE_PRECOMPUTED; }
    std::shared_ptr<Value> get_value() { return value; }
protected:
    std::shared_ptr<Value> value;
};
```

- [x] **Step 2:** `imports.h` declares `struct ImportState` and `bool expand_imports(const std::string&, const std::string&, ImportState&, std::string&, std::string&);` matching the current pseudo.cpp definitions.
- [x] **Step 3:** `make clean && make && make test` — all existing gtests PASS. Run an example to sanity-check. Commit.

### Task 3: Runtime library `libpseudort.a`

**Files:**
- Create: `src/runtime.h`, `src/runtime.cpp`
- Modify: `Makefile` (`runtime` target producing `build/libpseudort.a` from all interpreter objects except `shell.o`, plus `runtime.o`)

- [x] **Step 1:** `runtime.h` — extern "C" ABI (all value params/returns are raw `Value*` kept alive by arena frames):

```cpp
extern "C" {
void rt_init();
void rt_shutdown();
// constructors
Value* rt_make_int(int64_t v);
Value* rt_make_float(double v);
Value* rt_make_string(const char* s);
Value* rt_make_none();
Value* rt_array_new();
void rt_array_push(Value* arr, Value* v);
// variables (current scope chain)
Value* rt_get_var(const char* name);
Value* rt_set_var(const char* name, Value* v); // returns v
// operators: op is a single char code matching token types
Value* rt_bin_op(int op, Value* a, Value* b);
Value* rt_unary_op(int op, Value* a);
// control helpers
int64_t rt_cond_eq1(Value* v);     // stoll(get_num()) == 1  (if)
int64_t rt_as_int(Value* v);       // as_int()               (while/repeat)
int64_t rt_for_cond(Value* i, Value* end, Value* step);
Value* rt_for_step_check(Value* step); // errors on step == 0
// indexing / members
Value* rt_index(Value* obj, Value* idx);
Value* rt_index_assign(Value* obj, Value* idx, Value* v);
Value* rt_member_access(Value* obj, const char* name);
// calls
Value* rt_define_algo(const char* name, Value* (*fn)(), const char* const* args, int64_t nargs);
Value* rt_call(Value* callee, Value** argv, int64_t argc);
// frames
int64_t rt_frame_mark();
void rt_frame_release(int64_t mark);
Value* rt_keep(Value* v, int64_t mark); // re-register v in frame `mark`-1 (survives release)
}
```

- [x] **Step 2:** Implement in runtime.cpp: global `std::vector<std::vector<std::shared_ptr<Value>>> frames` and `std::vector<std::unique_ptr<SymbolTable>> scopes` (global scope created by `rt_init`); `CompiledAlgoValue : Value` holding `Value*(*fn)()` and arg names; `rt_call` dispatch (compiled → arity check with interpreter's exact colored error strings, new scope + frame, bind args, invoke; otherwise wrap args in `PrecomputedNode` and call `callee->execute(nodes, current_scope)`). Binary/unary ops call the existing `operator+` etc. on `shared_from_this()` handles. Any `VALUE_ERROR` encountered → print `get_num()` + `"\n"`, `exit(1)`.
- [x] **Step 3:** `make runtime` builds `build/libpseudort.a`. Commit.

### Task 4: Compiler skeleton + driver (milestone: arithmetic & print)

**Files:**
- Create: `src/compiler.h`, `src/compiler.cpp` (class `Compiler`: `bool compile(const NodeList& ast, llvm::Module&)`, error list with positions)
- Create: `src/pseudoc.cpp` (driver: args, expand_imports, lex/parse with run()-style error printing, codegen, verify, O2 PassBuilder, TargetMachine object emission, link via `c++`)
- Modify: `Makefile` (`compiler` target via `llvm-config`, default `/opt/homebrew/opt/llvm/bin/llvm-config` fallback; link `-lLLVM` from `--libdir` with rpath)

- [x] **Step 1:** Codegen for: ValueNode literals, VarAccess/VarAssign, BinOp/UnaryOp (`rt_bin_op` with op codes), AlgorithmCall (callee codegen + `rt_call`), statement sequencing in `main` (`rt_init` first, `ret i32 0` last). Unsupported node types → recorded compile error.
- [x] **Step 2:** Driver `--emit-llvm` and `-o`; runtime archive lookup (`--runtime-lib`, `$PSEUDO_RT_LIB`, `<exe dir>/build/libpseudort.a`).
- [x] **Step 3:** `make compiler`; `echo 'print("hi", 1 + 2 * 3)' > /tmp/t.ps && ./pseudoc /tmp/t.ps -o /tmp/t && /tmp/t` matches `./pseudo /tmp/t.ps`. Commit.

### Task 5: Control flow

**Files:**
- Modify: `src/compiler.cpp`

- [x] **Step 1:** `if` (yields Int 0 when untaken, propagates last-statement value), `for` (assign → step default/check → end; latch keeps previous `i` slot per visit_for; per-iteration `rt_frame_mark`/`rt_frame_release`), `while`, `repeat`, `break`/`continue` (branch + release to loop mark), nested loops via a loop-context stack.
- [x] **Step 2:** Differential run of `test/compiler/control_flow.ps`, `test/test_repeat.ps` — PASS. Commit.

### Task 6: Functions and return

**Files:**
- Modify: `src/compiler.cpp`

- [x] **Step 1:** AlgorithmDef → new LLVM function compiled with its own builder state (body statements; implicit result = last statement value; `ReturnNode` → `rt_keep` + `ret`); definition site emits `rt_define_algo` with arg-name global strings. Same handling when defs appear inside other bodies.
- [x] **Step 2:** Differential run of `test/compiler/functions.ps`, `test/test_fib.ps` — PASS. Commit.

### Task 7: Arrays, hash tables, strings, member calls

**Files:**
- Modify: `src/compiler.cpp`

- [x] **Step 1:** ArrayNode literal, ArrayAccess (`rt_index`), ArrayAssign (`rt_index_assign`, including hash-table set; member assignment on non-instances reports the interpreter's error string at runtime), MemberAccess (`rt_member_access` → BoundMethodValue path; method calls flow through existing `rt_call` fallback).
- [x] **Step 2:** Differential run of `test/compiler/collections.ps`, `test/test_array_methods.ps`, `test/test_string_index.ps` — PASS. Commit.

### Task 8: Struct rejection, error fixtures, full suite

**Files:**
- Modify: `src/compiler.cpp` (NODE_STRUCTDEF → "compile error: Struct is not supported by the compiler yet" with location; same for instance member assignment when detectable)
- Modify: `Makefile` (`test-compiler` runs harness)

- [x] **Step 1:** `bash test/compiler_tests.sh` — ALL PASS (including struct rejection and runtime error fixtures).
- [x] **Step 2:** `make test` still green; `ci/clang-format-check.sh` clean (format new files).
- [x] **Step 3:** Update README (compile section). Commit.

## Self-review notes

- Spec coverage: every spec section maps to a task (tests→1, intrusive changes→2, runtime→3, driver/build→4, codegen semantics→4–7, struct rejection→8, README/CI→8).
- Types consistent: ABI defined once in Task 3 and used in 4–7.
- `rt_keep`'s frame semantics are decided in Task 3 (`re-register in caller frame`), used by Task 6 return handling.

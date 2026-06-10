# LLVM AOT Compiler for PseudoCode — Design

Date: 2026-06-10
Status: Approved for implementation (autonomous session; decisions documented below)

## Goal

Add a compile mode to the project: a `pseudoc` binary that turns a `.ps` source
file into a native executable using LLVM, with output behavior identical to the
interpreter (`pseudo file.ps`).

## Approaches considered

1. **Boxed-value AOT with runtime library (chosen).** Compile the AST to LLVM
   IR where each language operation is a call into an `extern "C"` runtime
   (`rt_*`) built on top of the existing `Value`, `SymbolTable`, and
   `BoundMethodValue` code. Link the generated object file against
   `libpseudort.a` to produce a standalone executable.
   - Pros: exact semantic parity (reuses the interpreter's own operators,
     builtins, array/hash methods), tractable codegen, full dynamic typing.
   - Cons: values remain boxed, so speedups come from removing AST-walking
     overhead, not from unboxed arithmetic (future work).
2. **Typed-subset native codegen.** Infer int/float types and emit raw
   arithmetic. Fast, but supports only a fraction of the language and
   duplicates semantics; high divergence risk.
3. **Textual IR emission + clang.** No LLVM library dependency, but fragile,
   unverifiable IR and a worse foundation. Rejected.

## Architecture

```
pseudoc (new binary)
  ├── reuses: lexer, parser, import expansion (refactored out of run())
  ├── src/compiler.{h,cpp}: Compiler — AST → llvm::Module (LLVM C++ API)
  └── links output .o with build/libpseudort.a via the system C++ driver

libpseudort.a (new static library)
  ├── existing objects: pseudo.o, interpreter.o, symboltable.o, node.o, ...
  └── src/runtime.{h,cpp}: extern "C" rt_* shims + CompiledAlgoValue
```

### Value representation

All values are `Value*` (LLVM opaque `ptr`). `Value` gains
`std::enable_shared_from_this<Value>` so the runtime can recover the owning
`shared_ptr` from a raw pointer. Lifetimes are managed by an **arena frame
stack** in the runtime: every `rt_*` function that produces a value registers
its `shared_ptr` in the current frame. Frames are pushed/popped:

- per function call (by `rt_call`),
- per loop iteration (emitted by codegen), so long-running loops don't grow
  memory unboundedly. Values that survive (stored in variables, arrays, etc.)
  are kept alive by the `SymbolTable`/container `shared_ptr`s.
- `rt_frame_mark()` / `rt_frame_release(mark)` let codegen unwind frames on
  `break` / `return` edges. Function results are re-registered in the caller's
  frame.

### Codegen rules (mirror interpreter semantics exactly)

- Each top-level statement of the program body compiles into `main` in order;
  `Algorithm` definitions compile to LLVM functions `ptr @"ps.algo.N.<name>"()`
  plus a runtime registration (`rt_define_algo`) at the definition site, so
  call-before-definition errors behave the same.
- Variables: always through `rt_get_var` / `rt_set_var` (dynamic scoping via
  the symbol-table parent chain, exactly like `AlgoValue::execute`).
- `if`: branch on `stoll(cond->get_num()) == 1` (`rt_cond_eq1`); the statement
  yields Int 0 when no branch is taken.
- `for`: replicate `visit_for` — evaluate assign, then step (default Int 1),
  then end; `step == 0` → "Infinite for loop" error; the latch recomputes
  `var <- i + step` from the value read at the previous latch (body
  reassignments of the loop variable are overwritten, matching the
  interpreter). Condition is float-aware `<=` / `>=` via `rt_for_cond`.
- `while`: condition `as_int() == 1`; `repeat ... until`: body first, loop
  while `as_int() == 0`.
- `break`/`continue`/`return` compile to branches (with frame releases);
  `return` outside any algorithm behaves like the interpreter (top level:
  statement result, ignored).
- Calls: evaluate callee and args to `Value*`, then `rt_call(callee, argv,
  argc)`:
  - `CompiledAlgoValue` → arity check ("Too few/Too many arguments" with the
    same color codes), push scope + frame, bind args, call function pointer.
  - anything else (builtins, `BoundMethodValue` for array/string/hash methods)
    → wrap evaluated args in `PrecomputedNode`s and reuse the existing
    `execute(NodeList, SymbolTable*)` paths unchanged.
- Member access / array index / array assign: `rt_member_access`, `rt_index`,
  `rt_index_assign` mirroring `visit_member_access`, `visit_array_access`,
  `visit_array_assign` (string 1-indexing, hash get/set, array element
  assignment).
- Struct definitions: in-struct methods compile to private algorithm functions
  and runtime `CompiledAlgoValue`s, then `rt_define_struct` registers a
  `StructValue` with member and method maps. Later `Algorithm Struct::method`
  definitions compile as global algorithms and call `rt_struct_add_method` at
  the definition site, matching the interpreter's attachment timing. Bound
  instance method calls execute with a parent scope containing `self`.
- Array literals: `rt_array_new` + `rt_array_push`.
- Errors: any `rt_*` function that produces or receives a `VALUE_ERROR` prints
  `err->get_num()` (same text the interpreter prints at top level) and exits
  with status 1. There is no error-value plumbing in generated IR.

### Documented v1 divergences from the interpreter

- A loop used as a function's implicit return value evaluates to `NONE`;
  the interpreter (with `collect_loop_results`) returns an array of
  per-iteration values. Rarely used; revisit if needed.
- `break`/`continue` outside any loop are compile-time errors (the
  interpreter propagates them as control values, which can even break an
  outer loop across a function call — pathological behavior we do not
  reproduce).
- Compiled recursive pure-numeric algorithms get the interpreter's
  by-argument memoization via `rt_define_algo(..., memoizable)`, using the
  shared `is_memoizable_numeric_algo` analysis (src/analysis.h).

### Intrusive changes to existing code (kept minimal)

1. `Value` inherits `std::enable_shared_from_this<Value>` (value.h).
2. New `PrecomputedNode` (value.h, since it stores a `shared_ptr<Value>`) and
   one dispatch case in `Interpreter::visit`.
3. `expand_imports` (and its `ImportState`) refactored from an anonymous
   namespace in pseudo.cpp into a small header so the compiler driver can use
   it.

### Driver & CLI

```
pseudoc <file.ps> [-o <out>] [--emit-llvm] [--runtime-lib <path>]
```

- Default output: source basename without extension (`fib.ps` → `fib`).
- `--emit-llvm` prints the textual IR to stdout and stops.
- Pipeline: read → expand imports → lex → parse (reuse run()'s error
  reporting) → codegen → `llvm::verifyModule` → O2 via `PassBuilder` → object
  file (TargetMachine) → link `c++ obj libpseudort.a -o out`.
- Runtime archive located via `--runtime-lib`, `$PSEUDO_RT_LIB`, or relative to
  the `pseudoc` binary (`<dir>/build/libpseudort.a`).

### Build integration

Makefile gets optional targets (not part of default `make`, since LLVM is an
optional dependency):

- `make runtime` → `build/libpseudort.a` (existing objects + runtime.o).
- `make compiler` → `pseudoc`, compiled/linked using `llvm-config`
  (`/opt/homebrew/opt/llvm/bin/llvm-config` fallback). Fails with a clear
  message if LLVM is missing.
- `make test-compiler` → end-to-end differential tests.

CI is unchanged (compiler build is optional); new sources follow
clang-format/clang-tidy rules.

## Testing

Differential end-to-end testing is the core strategy:
`test/compiler_tests.sh` compiles each supported `.ps` test/benchmark file
with `pseudoc` and asserts its stdout equals `./pseudo file.ps` stdout.
Coverage: arithmetic/comparison/logic on ints/floats/strings, arrays +
methods, hash tables, string indexing, if/for/while/repeat with
break/continue, recursion (fib), nested functions, argument-count errors,
runtime errors (index out of range), and struct construction/method calls.
Existing `make test` (gtest) must keep passing.

## Performance expectation

v1 removes parse/AST-walk overhead but keeps boxed values and symbol-table
access; expect modest wins over the interpreter. Unboxing/SROA of numeric
locals is explicitly future work.

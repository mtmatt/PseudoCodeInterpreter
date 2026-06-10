# Benchmarks

This directory contains benchmark programs for the pseudocode interpreter,
native compiler output, Python 3.14, and Rust.

Run the full interpreter-vs-compiled suite:

```sh
python3 benchmark/run_benchmarks.py
```

Run one benchmark:

```sh
python3 benchmark/run_benchmarks.py --bench cpu_arithmetic
```

Compare against Python instead of compiled output:

```sh
python3 benchmark/run_benchmarks.py --compare python --python python3.14
```

Compare against Rust instead of compiled output:

```sh
python3 benchmark/run_benchmarks.py --compare rust
```

Compare both peer groups:

```sh
python3 benchmark/run_benchmarks.py --compare all --python python3.14
```

The runner builds `pseudo`, and for compiled comparisons also builds `pseudoc`
and `build/libpseudort.a`. For Rust comparisons it compiles each matching
`.rs` fixture with `rustc -C opt-level=3`. Each benchmark's stdout must match
the interpreter. The table reports median wall time across the requested
iterations. The `compile` column is one compile/link pass and is not included
in runtime. On macOS, RSS is collected with `/usr/bin/time -l` when available;
restricted environments fall back to runtime only and show `n/a`.

Latest local result for interpreter vs Python 3.14.4, 10 iterations, 2 warmups:

| benchmark | output | interpreter | Python | Python speedup | interp RSS | Python RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.0312s | 0.0296s | 1.1x | n/a | n/a |
| recursive_fib | 46368 | 0.0032s | 0.0248s | 0.1x | n/a | n/a |
| function_calls | 982568 | 0.0231s | 0.0224s | 1.0x | n/a | n/a |
| array_memory | 25194337 | 0.0215s | 0.0236s | 0.9x | n/a | n/a |
| array_push_pop | 24896907 | 0.0202s | 0.0225s | 0.9x | n/a | n/a |
| string_concat | 20000 | 0.0095s | 0.0206s | 0.5x | n/a | n/a |
| rbtree_ordered_set | 1200 17 99853 17 black | 0.0884s | 0.0183s | 4.8x | n/a | n/a |
| btree_ordered_set | 1200 17 99853 17 3 | 0.1738s | 0.0185s | 9.4x | n/a | n/a |

Latest local result for compiled output vs Rust 1.91.0, 10 iterations, 2 warmups:

| benchmark | output | compiled | Rust | Rust speedup | compiled RSS | Rust RSS | pseudoc | rustc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.0043s | 0.0032s | 1.3x | n/a | n/a | 0.0433s | 0.0974s |
| recursive_fib | 46368 | 0.0028s | 0.0028s | 1.0x | n/a | n/a | 0.0574s | 0.1025s |
| function_calls | 982568 | 0.0031s | 0.0031s | 1.0x | n/a | n/a | 0.0562s | 0.0895s |
| array_memory | 25194337 | 0.0124s | 0.0027s | 4.5x | n/a | n/a | 0.0450s | 0.1011s |
| array_push_pop | 24896907 | 0.0099s | 0.0027s | 3.7x | n/a | n/a | 0.0450s | 0.0986s |
| string_concat | 20000 | 0.0148s | 0.0026s | 5.7x | n/a | n/a | 0.0435s | 0.0960s |
| rbtree_ordered_set | 1200 17 99853 17 black | 0.0391s | 0.0029s | 13.4x | n/a | n/a | 0.1040s | 0.1306s |
| btree_ordered_set | 1200 17 99853 17 3 | 0.0691s | 0.0027s | 25.3x | n/a | n/a | 0.1084s | 0.1319s |

`Python speedup` is `interpreter / Python`; values above 1 mean Python was
faster. `Rust speedup` is `compiled / Rust`; values above 1 mean Rust was
faster. The compiled backend now lowers proven integer locals, expressions,
simple integer `for` loops, simple integer algorithm calls, and numeric array
read/write/push/pop operations to native integer code. It still falls back to
the generic runtime for dynamic values, strings, complex control flow, structs,
and library data structures.

Notes:

- Timings include process startup for both executables.
- The JIT currently specializes numeric expressions, numeric array reads,
  numeric `push`/`pop`, simple loop-body assignments in file mode, and
  single-return numeric function calls in hot file-mode loops. It still does
  not generate native machine code.
- `recursive_fib` mainly measures recursive numeric calls. Pure self-recursive
  numeric algorithms are memoized after conservative AST analysis.
- `array_memory` exercises resize, indexed writes, indexed reads, and integer
  arithmetic.
- `array_push_pop` exercises dynamic array growth and shrinking.
- `rbtree_ordered_set` compares `RBTree` insert/contains/min/max against a
  Python ordered set implemented with `bisect` and Rust's `BTreeSet`.
- `btree_ordered_set` compares `BTree` insert/contains/min/max/height against a
  Python ordered set implemented with `bisect` and Rust's `BTreeSet`.
- Array member calls have direct native and bytecode fast paths for `push`,
  `pop`, `resize`, `size`, and `back`, avoiding bound-method allocation on hot
  paths.
- File execution discards unused loop result arrays. REPL/default interpreter
  mode still preserves loop expression results.
- `string_concat` prints only the loop count, but still builds the string using
  a guarded in-place self-concat path when the string is not aliased.

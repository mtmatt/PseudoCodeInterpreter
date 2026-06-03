# Benchmarks

This directory contains paired benchmark programs for the pseudocode
interpreter and Python 3.14.

Run the full suite:

```sh
python3.14 benchmark/run_benchmarks.py
```

Run one benchmark:

```sh
python3.14 benchmark/run_benchmarks.py --bench cpu_arithmetic
```

The runner builds `shell`, executes each `.ps` program and matching `.py`
program, verifies that stdout matches, then reports median wall time across the
requested iterations. On macOS it also reports peak resident memory using
`/usr/bin/time -l`.

Latest local result on Python 3.14.5, 5 iterations, 1 warmup, with adaptive
numeric expression JIT, file-mode loop specialization, pure recursive
memoization, and simple numeric function inlining enabled:

| benchmark | output | pseudo | Python 3.14 | slowdown | pseudo RSS | Python RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.0281s | 0.0283s | 1.0x | 2.2 MiB | 14.5 MiB |
| recursive_fib | 46368 | 0.0037s | 0.0190s | 0.2x | 2.2 MiB | 14.4 MiB |
| function_calls | 982568 | 0.0180s | 0.0206s | 0.9x | 2.3 MiB | 14.4 MiB |
| array_memory | 25194337 | 0.0189s | 0.0225s | 0.8x | 9.1 MiB | 15.8 MiB |
| array_push_pop | 24896907 | 0.0175s | 0.0217s | 0.8x | 7.1 MiB | 16.1 MiB |
| string_concat | 20000 | 0.0094s | 0.0194s | 0.5x | 2.2 MiB | 14.5 MiB |

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
  Python ordered set implemented with `bisect`.
- `btree_ordered_set` compares `BTree` insert/contains/min/max/height against a
  Python ordered set implemented with `bisect`.
- Array member calls have direct native and bytecode fast paths for `push`,
  `pop`, `resize`, `size`, and `back`, avoiding bound-method allocation on hot
  paths.
- File execution discards unused loop result arrays. REPL/default interpreter
  mode still preserves loop expression results.
- `string_concat` prints only the loop count, but still builds the string using
  a guarded in-place self-concat path when the string is not aliased.

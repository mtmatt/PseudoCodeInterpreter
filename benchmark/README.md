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
numeric expression JIT and shared function-body expression caching enabled:

| benchmark | output | pseudo | Python 3.14 | slowdown | pseudo RSS | Python RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.0531s | 0.0326s | 1.6x | 27.7 MiB | 14.5 MiB |
| recursive_fib | 46368 | 0.1120s | 0.0242s | 4.6x | 2.3 MiB | 14.5 MiB |
| function_calls | 982568 | 0.0426s | 0.0446s | 1.0x | 9.8 MiB | 14.6 MiB |
| array_memory | 25194337 | 0.0879s | 0.0488s | 1.8x | 17.2 MiB | 16.1 MiB |
| array_push_pop | 24896907 | 0.0843s | 0.0405s | 2.1x | 15.4 MiB | 16.2 MiB |
| string_concat | 20000 | 0.0572s | 0.0358s | 1.6x | 225.1 MiB | 14.8 MiB |

Notes:

- Timings include process startup for both executables.
- The JIT currently specializes hot numeric expressions; it does not compile
  functions, arrays, strings, structs, imports, or native machine code.
- `recursive_fib` mainly measures function call and recursion overhead. The
  shared expression cache lets numeric expressions inside functions become hot
  across calls.
- `array_memory` exercises resize, indexed writes, indexed reads, and integer
  arithmetic.
- `array_push_pop` exercises dynamic array growth and shrinking.
- Array member calls have a direct native fast path for `push`, `pop`,
  `resize`, `size`, and `back`, avoiding bound-method allocation and temporary
  call scopes.
- `string_concat` prints only the loop count, but still builds the string.

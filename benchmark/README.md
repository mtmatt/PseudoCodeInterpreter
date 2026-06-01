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
numeric expression JIT enabled:

| benchmark | output | pseudo | Python 3.14 | slowdown | pseudo RSS | Python RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.0538s | 0.0329s | 1.6x | 27.7 MiB | 14.5 MiB |
| recursive_fib | 46368 | 0.2218s | 0.0241s | 9.2x | 2.4 MiB | 14.5 MiB |
| function_calls | 982568 | 0.1118s | 0.0251s | 4.5x | 9.8 MiB | 14.5 MiB |
| array_memory | 25194337 | 0.0709s | 0.0266s | 2.7x | 15.0 MiB | 15.9 MiB |
| array_push_pop | 24896907 | 0.1081s | 0.0262s | 4.1x | 14.1 MiB | 16.2 MiB |
| string_concat | 20000 | 0.0392s | 0.0236s | 1.7x | 225.0 MiB | 14.6 MiB |

Notes:

- Timings include process startup for both executables.
- The JIT currently specializes hot numeric expressions; it does not compile
  functions, arrays, strings, structs, imports, or native machine code.
- `recursive_fib` mainly measures function call and recursion overhead.
- `array_memory` exercises resize, indexed writes, indexed reads, and integer
  arithmetic.
- `array_push_pop` exercises dynamic array growth and shrinking.
- `string_concat` prints only the loop count, but still builds the string.

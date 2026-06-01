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

Latest local result on Python 3.14.5, 3 iterations, 1 warmup:

| benchmark | output | pseudo | Python 3.14 | slowdown | pseudo RSS | Python RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cpu_arithmetic | 9599988 | 0.2205s | 0.0314s | 7.0x | 27.6 MiB | 14.5 MiB |
| recursive_fib | 46368 | 0.1994s | 0.0238s | 8.4x | 2.2 MiB | 14.5 MiB |
| function_calls | 982568 | 0.0997s | 0.0238s | 4.2x | 9.7 MiB | 14.4 MiB |
| array_memory | 25194337 | 0.0907s | 0.0260s | 3.5x | 16.3 MiB | 15.9 MiB |
| array_push_pop | 24896907 | 0.1011s | 0.0255s | 4.0x | 15.5 MiB | 16.1 MiB |
| string_concat | 20000 | 0.0406s | 0.0236s | 1.7x | 225.0 MiB | 14.6 MiB |

Notes:

- Timings include process startup for both executables.
- `recursive_fib` mainly measures function call and recursion overhead.
- `array_memory` exercises resize, indexed writes, indexed reads, and integer
  arithmetic.
- `array_push_pop` exercises dynamic array growth and shrinking.
- `string_concat` prints only the loop count, but still builds the string.

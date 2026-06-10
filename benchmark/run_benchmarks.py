#!/usr/bin/env python3
import argparse
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PSEUDO_DIR = ROOT / "benchmark" / "pseudo"
PYTHON_DIR = ROOT / "benchmark" / "python"
RUST_DIR = ROOT / "benchmark" / "rust"

BENCHMARKS = [
    "cpu_arithmetic",
    "recursive_fib",
    "function_calls",
    "array_memory",
    "array_push_pop",
    "string_concat",
    "rbtree_ordered_set",
    "btree_ordered_set",
]

MAX_RSS_RE = re.compile(r"^\s*(\d+)\s+maximum resident set size", re.MULTILINE)
TIME_PERMISSION_ERROR = "time: sysctl kern.clockrate: Operation not permitted"
TIME_L_DISABLED = False


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Compare PseudoCodeInterpreter benchmark programs across the "
            "interpreter, native compiler output, Python, and Rust."
        )
    )
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument(
        "--compare",
        choices=("compiled", "python", "rust", "both", "all"),
        default="compiled",
        help=(
            "benchmark comparison to run; python compares interpreter vs Python, "
            "rust compares compiled pseudocode vs Rust, 'all' runs those two "
            "peer comparisons, and 'both' means compiled+python for backward "
            "compatibility"
        ),
    )
    parser.add_argument("--python", default="python3.14")
    parser.add_argument("--rustc", default="rustc")
    parser.add_argument("--interpreter", default=str(ROOT / "pseudo"))
    parser.add_argument("--shell", help="deprecated alias for --interpreter")
    parser.add_argument("--compiler", default=str(ROOT / "pseudoc"))
    parser.add_argument("--runtime-lib")
    parser.add_argument("--bench", choices=BENCHMARKS, action="append")
    args = parser.parse_args()
    if args.shell:
        args.interpreter = args.shell
    if args.iterations <= 0:
        parser.error("--iterations must be greater than zero")
    if args.warmups < 0:
        parser.error("--warmups cannot be negative")
    return args


def run_plain_command(command):
    start = time.perf_counter()
    proc = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    elapsed = time.perf_counter() - start
    return proc, elapsed, None, proc.stderr.strip()


def run_command(command):
    global TIME_L_DISABLED

    time_bin = "/usr/bin/time" if platform.system() == "Darwin" else None
    if time_bin and not Path(time_bin).exists():
        time_bin = shutil.which("time")
    if time_bin and not TIME_L_DISABLED:
        wrapped = [time_bin, "-l", *command]
        start = time.perf_counter()
        proc = subprocess.run(
            wrapped,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        elapsed = time.perf_counter() - start
        match = MAX_RSS_RE.search(proc.stderr)
        max_rss = int(match.group(1)) if match else None
        stderr = MAX_RSS_RE.sub("", proc.stderr).strip()
        if proc.returncode != 0 and TIME_PERMISSION_ERROR in proc.stderr:
            TIME_L_DISABLED = True
            proc, elapsed, max_rss, stderr = run_plain_command(command)
    else:
        proc, elapsed, max_rss, stderr = run_plain_command(command)

    if proc.returncode != 0:
        joined = " ".join(command)
        raise RuntimeError(
            f"{joined} exited with {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{stderr}"
        )

    return proc.stdout.rstrip("\n"), elapsed, max_rss


def measure(label, command, warmups, iterations):
    output = None
    for _ in range(warmups):
        output, _, _ = run_command(command)

    times = []
    rss_values = []
    for _ in range(iterations):
        output, elapsed, max_rss = run_command(command)
        times.append(elapsed)
        if max_rss is not None:
            rss_values.append(max_rss)

    return {
        "label": label,
        "output": output,
        "median": statistics.median(times),
        "min": min(times),
        "max": max(times),
        "rss": max(rss_values) if rss_values else None,
    }


def compile_pseudo(compiler, pseudo_file, output_path, runtime_lib):
    command = [str(compiler), str(pseudo_file), "-o", str(output_path)]
    if runtime_lib:
        command.extend(["--runtime-lib", str(runtime_lib)])
    _, elapsed, _ = run_command(command)
    return elapsed


def compile_rust(rustc, rust_file, output_path):
    command = [
        rustc,
        str(rust_file),
        "-C",
        "opt-level=3",
        "-C",
        "debuginfo=0",
        "-o",
        str(output_path),
    ]
    _, elapsed, _ = run_command(command)
    return elapsed


def format_time(seconds):
    return f"{seconds:.4f}s"


def format_rss(value):
    if value is None:
        return "n/a"
    # macOS /usr/bin/time -l reports bytes.
    return f"{value / (1024 * 1024):.1f} MiB"


def output_digest(output):
    if len(output) <= 80:
        return output
    return f"{len(output)} chars"


def print_compiled_table(rows):
    print(
        f"{'benchmark':<20} {'output':<24} {'interpreter':>12} {'compiled':>10} "
        f"{'speedup':>9} {'interp rss':>12} {'compiled rss':>12} {'compile':>10}"
    )
    print("-" * 116)
    for row in rows:
        interpreter = row["interpreter"]
        compiled = row["compiled"]
        speedup = interpreter["median"] / compiled["median"]
        print(
            f"{row['name']:<20} {output_digest(interpreter['output']):<24} "
            f"{format_time(interpreter['median']):>12} {format_time(compiled['median']):>10} "
            f"{speedup:>8.1f}x {format_rss(interpreter['rss']):>12} "
            f"{format_rss(compiled['rss']):>12} {format_time(row['compile_time']):>10}"
        )


def print_python_table(rows):
    print(
        f"{'benchmark':<20} {'output':<24} {'interpreter':>12} {'python':>10} "
        f"{'python speedup':>14} {'interp rss':>12} {'python rss':>12}"
    )
    print("-" * 109)
    for row in rows:
        interpreter = row["interpreter"]
        python = row["python"]
        speedup = interpreter["median"] / python["median"]
        print(
            f"{row['name']:<20} {output_digest(interpreter['output']):<24} "
            f"{format_time(interpreter['median']):>12} {format_time(python['median']):>10} "
            f"{speedup:>13.1f}x {format_rss(interpreter['rss']):>12} "
            f"{format_rss(python['rss']):>12}"
        )


def print_rust_table(rows):
    print(
        f"{'benchmark':<20} {'output':<24} {'compiled':>10} {'rust':>10} "
        f"{'rust speedup':>13} {'compiled rss':>12} {'rust rss':>12} "
        f"{'pseudoc':>10} {'rustc':>10}"
    )
    print("-" * 127)
    for row in rows:
        compiled = row["compiled"]
        rust = row["rust"]
        speedup = compiled["median"] / rust["median"]
        print(
            f"{row['name']:<20} {output_digest(compiled['output']):<24} "
            f"{format_time(compiled['median']):>10} {format_time(rust['median']):>10} "
            f"{speedup:>12.1f}x {format_rss(compiled['rss']):>12} "
            f"{format_rss(rust['rss']):>12} {format_time(row['compile_time']):>10} "
            f"{format_time(row['rust_compile_time']):>10}"
        )


def main():
    args = parse_args()
    benches = args.bench or BENCHMARKS
    report_compiled = args.compare in ("compiled", "both")
    compare_python = args.compare in ("python", "both", "all")
    compare_rust = args.compare in ("rust", "all")
    measure_compiled = report_compiled or compare_rust

    subprocess.run(["make"], cwd=ROOT, check=True)
    if measure_compiled:
        subprocess.run(["make", "compiler"], cwd=ROOT, check=True)

    interpreter_bin = Path(args.interpreter)
    if not interpreter_bin.exists():
        raise FileNotFoundError(interpreter_bin)
    if measure_compiled and not Path(args.compiler).exists():
        raise FileNotFoundError(args.compiler)

    rows = []
    with tempfile.TemporaryDirectory(prefix="pseudo-bench-") as tmp_dir:
        tmp_dir = Path(tmp_dir)
        for name in benches:
            pseudo_file = PSEUDO_DIR / f"{name}.ps"
            row = {"name": name}
            interpreter = measure(
                "interpreter",
                [str(interpreter_bin), str(pseudo_file)],
                args.warmups,
                args.iterations,
            )
            row["interpreter"] = interpreter

            if measure_compiled:
                compiled_bin = tmp_dir / name
                row["compile_time"] = compile_pseudo(
                    args.compiler, pseudo_file, compiled_bin, args.runtime_lib
                )
                compiled = measure(
                    "compiled",
                    [str(compiled_bin)],
                    args.warmups,
                    args.iterations,
                )
                if interpreter["output"] != compiled["output"]:
                    raise AssertionError(
                        f"{name} output mismatch\n"
                        f"interpreter: {interpreter['output']!r}\n"
                        f"compiled: {compiled['output']!r}"
                    )
                row["compiled"] = compiled

            if compare_python:
                python_file = PYTHON_DIR / f"{name}.py"
                python = measure(
                    "python",
                    [args.python, str(python_file)],
                    args.warmups,
                    args.iterations,
                )
                if interpreter["output"] != python["output"]:
                    raise AssertionError(
                        f"{name} output mismatch\n"
                        f"interpreter: {interpreter['output']!r}\n"
                        f"python: {python['output']!r}"
                    )
                row["python"] = python

            if compare_rust:
                rust_file = RUST_DIR / f"{name}.rs"
                rust_bin = tmp_dir / f"{name}-rust"
                row["rust_compile_time"] = compile_rust(args.rustc, rust_file, rust_bin)
                rust = measure(
                    "rust",
                    [str(rust_bin)],
                    args.warmups,
                    args.iterations,
                )
                if interpreter["output"] != rust["output"]:
                    raise AssertionError(
                        f"{name} output mismatch\n"
                        f"interpreter: {interpreter['output']!r}\n"
                        f"rust: {rust['output']!r}"
                    )
                row["rust"] = rust

            rows.append(row)

    print(f"Interpreter: {interpreter_bin}")
    if measure_compiled:
        print(f"Compiler: {args.compiler}")
    if compare_python:
        print(f"Python: {subprocess.check_output([args.python, '--version'], text=True).strip()}")
    if compare_rust:
        print(f"Rust: {subprocess.check_output([args.rustc, '--version'], text=True).strip()}")
    print(f"Iterations: {args.iterations}, warmups: {args.warmups}")
    print()
    if report_compiled:
        print_compiled_table(rows)
    if compare_python:
        if report_compiled:
            print()
        print_python_table(rows)
    if compare_rust:
        if report_compiled or compare_python:
            print()
        print_rust_table(rows)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"benchmark failed: {exc}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3.14
import argparse
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PSEUDO_DIR = ROOT / "benchmark" / "pseudo"
PYTHON_DIR = ROOT / "benchmark" / "python"

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


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compare PseudoCodeInterpreter benchmark programs with Python 3.14."
    )
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--python", default="python3.14")
    parser.add_argument("--shell", default=str(ROOT / "shell"))
    parser.add_argument("--bench", choices=BENCHMARKS, action="append")
    return parser.parse_args()


def run_command(command):
    time_bin = "/usr/bin/time" if platform.system() == "Darwin" else None
    if time_bin and not Path(time_bin).exists():
        time_bin = shutil.which("time")
    if time_bin:
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
    else:
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
        max_rss = None
        stderr = proc.stderr.strip()

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


def main():
    args = parse_args()
    benches = args.bench or BENCHMARKS

    subprocess.run(["make"], cwd=ROOT, check=True)

    shell = Path(args.shell)
    if not shell.exists():
        raise FileNotFoundError(shell)

    rows = []
    for name in benches:
        pseudo_file = PSEUDO_DIR / f"{name}.ps"
        python_file = PYTHON_DIR / f"{name}.py"
        pseudo = measure(
            "pseudo",
            [str(shell), str(pseudo_file)],
            args.warmups,
            args.iterations,
        )
        python = measure(
            "python",
            [args.python, str(python_file)],
            args.warmups,
            args.iterations,
        )

        if pseudo["output"] != python["output"]:
            raise AssertionError(
                f"{name} output mismatch\npseudo: {pseudo['output']!r}\npython: {python['output']!r}"
            )

        rows.append((name, pseudo, python))

    print(f"Python: {subprocess.check_output([args.python, '--version'], text=True).strip()}")
    print(f"Iterations: {args.iterations}, warmups: {args.warmups}")
    print()
    print(
        f"{'benchmark':<20} {'output':<24} {'pseudo':>10} {'python':>10} "
        f"{'slowdown':>10} {'pseudo rss':>12} {'python rss':>12}"
    )
    print("-" * 91)
    for name, pseudo, python in rows:
        slowdown = pseudo["median"] / python["median"]
        print(
            f"{name:<20} {output_digest(pseudo['output']):<24} "
            f"{format_time(pseudo['median']):>10} {format_time(python['median']):>10} "
            f"{slowdown:>9.1f}x {format_rss(pseudo['rss']):>12} {format_rss(python['rss']):>12}"
        )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"benchmark failed: {exc}", file=sys.stderr)
        sys.exit(1)

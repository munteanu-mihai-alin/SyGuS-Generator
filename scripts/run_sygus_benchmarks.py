from __future__ import annotations

import argparse
import gzip
import json
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_SUFFIXES = (".sl", ".smt2", ".sy", ".sygus", ".sl.gz", ".smt2.gz")
DEFAULT_EXCLUDES = {
    "ICFP",
    ".git",
    ".idea",
    ".venv",
    "__pycache__",
}


@dataclass
class CommandResult:
    status: str
    exit_code: int | None
    duration_ms: int
    stdout: str
    stderr: str
    command: str


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_solver_command() -> str:
    parser_exe = project_root() / "build" / "sygus_parse"
    parser_exe_win = project_root() / "build" / "sygus_parse.exe"
    if parser_exe_win.exists():
        return f'"{parser_exe_win}" --parse-only "{{input}}"'
    return f'"{parser_exe}" --parse-only "{{input}}"'


def default_baseline_command(timeout_seconds: int) -> str:
    timeout_ms = timeout_seconds * 1000
    return (
        f'cvc5 --lang=sygus2 --parse-only --tlimit={timeout_ms} "{{input}}"'
    )


def resolve_benchmark_root(root: Path, year: str | None) -> Path:
    if (root / "comp").is_dir():
        return root / "comp" / year if year else root / "comp"
    return root / year if year else root


def should_skip(path: Path, exclude_names: set[str]) -> bool:
    return any(part in exclude_names for part in path.parts)


def iter_benchmarks(root: Path, suffixes: Iterable[str], exclude_names: set[str]) -> list[Path]:
    benchmarks: list[Path] = []
    for path in root.rglob("*"):
        if should_skip(path, exclude_names):
            continue
        if not path.is_file():
            continue
        if any(str(path).endswith(suffix) for suffix in suffixes):
            benchmarks.append(path)
    return sorted(benchmarks)


def materialize_input(path: Path) -> tuple[Path, tempfile.TemporaryDirectory[str] | None]:
    if path.suffix != ".gz":
        return path, None

    temp_dir = tempfile.TemporaryDirectory(prefix="sygus-benchmark-")
    uncompressed_path = Path(temp_dir.name) / path.stem
    with gzip.open(path, "rb") as source, open(uncompressed_path, "wb") as destination:
        destination.write(source.read())
    return uncompressed_path, temp_dir


def run_command(template: str, benchmark: Path, timeout_seconds: int) -> CommandResult:
    materialized_path, temp_dir = materialize_input(benchmark)
    try:
        command = template.format(input=str(materialized_path))
        start = time.perf_counter()
        try:
            completed = subprocess.run(
                command,
                shell=True,
                capture_output=True,
                text=True,
                timeout=timeout_seconds,
            )
            duration_ms = int((time.perf_counter() - start) * 1000)
            status = "ok" if completed.returncode == 0 else "failed"
            return CommandResult(
                status=status,
                exit_code=completed.returncode,
                duration_ms=duration_ms,
                stdout=completed.stdout,
                stderr=completed.stderr,
                command=command,
            )
        except subprocess.TimeoutExpired as error:
            duration_ms = int((time.perf_counter() - start) * 1000)
            return CommandResult(
                status="timeout",
                exit_code=None,
                duration_ms=duration_ms,
                stdout=error.stdout or "",
                stderr=error.stderr or "",
                command=command,
            )
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the local SyGuS parser and an optional baseline over benchmark trees."
    )
    parser.add_argument("benchmark_root", help="Benchmark root or SyGuS benchmark repo root")
    parser.add_argument("--year", help="Competition year to run, e.g. 2019")
    parser.add_argument("--timeout", type=int, default=20, help="Per-benchmark timeout in seconds")
    parser.add_argument(
        "--solver-command",
        default=None,
        help='Command template for the local tool, e.g. \'"build/sygus_parse --parse-only {input}"\'',
    )
    parser.add_argument(
        "--baseline-command",
        default=None,
        help='Command template for the baseline solver, e.g. \'cvc5 --parse-only {input}\'',
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to exclude while scanning",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Optional JSON output path; defaults under results/benchmark-runs/",
    )
    args = parser.parse_args()

    benchmark_root = resolve_benchmark_root(Path(args.benchmark_root), args.year)
    if not benchmark_root.exists():
        raise FileNotFoundError(f"Benchmark root does not exist: {benchmark_root}")

    exclude_names = set(DEFAULT_EXCLUDES)
    exclude_names.update(args.exclude)

    benchmarks = iter_benchmarks(benchmark_root, DEFAULT_SUFFIXES, exclude_names)
    if not benchmarks:
        raise RuntimeError(f"No benchmark files found under {benchmark_root}")

    solver_command = args.solver_command or default_solver_command()
    baseline_command = args.baseline_command or default_baseline_command(args.timeout)

    print(f"Benchmark root: {benchmark_root}")
    print(f"Benchmarks found: {len(benchmarks)}")
    print(f"Solver command: {solver_command}")
    print(f"Baseline command: {baseline_command}")

    results = []
    solver_ok = 0
    baseline_ok = 0

    for index, benchmark in enumerate(benchmarks, start=1):
        print(f"[{index}/{len(benchmarks)}] {benchmark}")
        solver_result = run_command(solver_command, benchmark, args.timeout)
        baseline_result = run_command(baseline_command, benchmark, args.timeout)

        if solver_result.status == "ok":
            solver_ok += 1
        if baseline_result.status == "ok":
            baseline_ok += 1

        results.append(
            {
                "benchmark": str(benchmark),
                "solver": asdict(solver_result),
                "baseline": asdict(baseline_result),
            }
        )

    output_path = (
        Path(args.output)
        if args.output
        else project_root()
        / "results"
        / "benchmark-runs"
        / f"benchmark-run-{int(time.time())}.json"
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "benchmark_root": str(benchmark_root),
        "solver_command": solver_command,
        "baseline_command": baseline_command,
        "solver_successes": solver_ok,
        "baseline_successes": baseline_ok,
        "benchmark_count": len(benchmarks),
        "results": results,
    }
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(f"Solver successes: {solver_ok}/{len(benchmarks)}")
    print(f"Baseline successes: {baseline_ok}/{len(benchmarks)}")
    print(f"Saved results to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

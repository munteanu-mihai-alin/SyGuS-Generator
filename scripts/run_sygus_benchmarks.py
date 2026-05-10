from __future__ import annotations

import argparse
import gzip
import json
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


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
    parsed: dict[str, Any] | None = None


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_solver_command() -> str:
    solver_exe = project_root() / "build" / "sygus_solve"
    solver_exe_win = project_root() / "build" / "sygus_solve.exe"
    if solver_exe_win.exists():
        return f'"{solver_exe_win}" --json "{{input}}"'
    return f'"{solver_exe}" --json "{{input}}"'


def bundled_cvc5_path() -> Path | None:
    candidates = [
        project_root() / "dependencies" / "linux" / "install" / "bin" / "cvc5",
        project_root() / "dependencies" / "ucrt64" / "install" / "bin" / "cvc5.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def default_baseline_command(timeout_seconds: int) -> str:
    timeout_ms = timeout_seconds * 1000
    cvc5_binary = bundled_cvc5_path()
    if cvc5_binary is not None:
        return f'"{cvc5_binary}" --lang=sygus2 --tlimit={timeout_ms} "{{input}}"'
    return f'cvc5 --lang=sygus2 --tlimit={timeout_ms} "{{input}}"'


def resolve_benchmark_root(root: Path, year: str | None) -> Path:
    if (root / "comp").is_dir():
        return root / "comp" / year if year else root / "comp"
    return root / year if year else root


def should_skip(path: Path, exclude_names: set[str]) -> bool:
    return any(part in exclude_names for part in path.parts)


def iter_benchmarks(
    root: Path, suffixes: Iterable[str], exclude_names: set[str]
) -> list[Path]:
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


def parse_json_payload(stdout: str) -> dict[str, Any] | None:
    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError:
        return None
    return payload if isinstance(payload, dict) else None


def run_command(
    template: str, benchmark: Path, timeout_seconds: int, parse_json: bool = False
) -> CommandResult:
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
            payload = parse_json_payload(completed.stdout) if parse_json else None
            if completed.returncode != 0:
                status = "failed"
            elif payload is not None and isinstance(payload.get("status"), str):
                status = str(payload["status"])
            else:
                status = "ok"
            return CommandResult(
                status=status,
                exit_code=completed.returncode,
                duration_ms=duration_ms,
                stdout=completed.stdout,
                stderr=completed.stderr,
                command=command,
                parsed=payload,
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


def track_name(root: Path, benchmark: Path) -> str:
    try:
        relative = benchmark.relative_to(root)
    except ValueError:
        return "unknown"

    if len(relative.parts) <= 1:
        return relative.parent.as_posix() or "."
    return relative.parts[0]


def increment(counter: dict[str, int], key: str) -> None:
    counter[key] = counter.get(key, 0) + 1


def build_track_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    for entry in results:
        track = entry["track"]
        bucket = summary.setdefault(
            track,
            {
                "benchmark_count": 0,
                "solver_status_counts": {},
                "baseline_status_counts": {},
            },
        )
        bucket["benchmark_count"] += 1
        increment(bucket["solver_status_counts"], entry["solver"]["status"])
        increment(bucket["baseline_status_counts"], entry["baseline"]["status"])
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the local SyGuS solver and a cvc5 baseline over benchmark trees."
    )
    parser.add_argument("benchmark_root", help="Benchmark root or SyGuS benchmark repo root")
    parser.add_argument("--year", help="Competition year to run, e.g. 2019")
    parser.add_argument("--timeout", type=int, default=20, help="Per-benchmark timeout in seconds")
    parser.add_argument(
        "--solver-command",
        default=None,
        help='Command template for the local solver, e.g. \'"build/sygus_solve --json {input}"\'',
    )
    parser.add_argument(
        "--baseline-command",
        default=None,
        help='Command template for the baseline solver, e.g. \'cvc5 --lang=sygus2 {input}\'',
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Directory name to exclude while scanning",
    )
    parser.add_argument(
        "--max-benchmarks",
        type=int,
        default=None,
        help="Optional hard cap on the number of benchmarks to run",
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
    if args.max_benchmarks is not None:
        benchmarks = benchmarks[: max(0, args.max_benchmarks)]
    if not benchmarks:
        raise RuntimeError(f"No benchmark files found under {benchmark_root}")

    solver_command = args.solver_command or default_solver_command()
    baseline_command = args.baseline_command or default_baseline_command(args.timeout)

    print(f"Benchmark root: {benchmark_root}")
    print(f"Benchmarks found: {len(benchmarks)}")
    print(f"Solver command: {solver_command}")
    print(f"Baseline command: {baseline_command}")

    results = []
    solver_status_counts: dict[str, int] = {}
    baseline_status_counts: dict[str, int] = {}

    for index, benchmark in enumerate(benchmarks, start=1):
        track = track_name(benchmark_root, benchmark)
        print(f"[{index}/{len(benchmarks)}] track={track} file={benchmark}")
        solver_result = run_command(
            solver_command, benchmark, args.timeout, parse_json=True
        )
        baseline_result = run_command(
            baseline_command, benchmark, args.timeout, parse_json=False
        )

        increment(solver_status_counts, solver_result.status)
        increment(baseline_status_counts, baseline_result.status)

        results.append(
            {
                "benchmark": str(benchmark),
                "relative_path": str(benchmark.relative_to(benchmark_root)),
                "track": track,
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
        "benchmark_count": len(benchmarks),
        "solver_status_counts": solver_status_counts,
        "baseline_status_counts": baseline_status_counts,
        "track_summary": build_track_summary(results),
        "results": results,
    }
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(f"Solver statuses: {solver_status_counts}")
    print(f"Baseline statuses: {baseline_status_counts}")
    print(f"Saved results to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

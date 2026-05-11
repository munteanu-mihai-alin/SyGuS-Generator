from __future__ import annotations

import argparse
import gzip
import json
import os
import shutil
import subprocess
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


DEFAULT_TIMEOUT_SECONDS = 5
DEFAULT_WORKERS = 10
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


def first_existing_path(candidates: Iterable[Path]) -> Path | None:
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def discover_solver_binary() -> Path | None:
    root = project_root()
    candidates = [
        root / "build-ucrt" / "sygus_solve.exe",
        root / "build" / "sygus_solve.exe",
        root / "build-ucrt" / "sygus_solve",
        root / "build" / "sygus_solve",
    ]
    discovered = first_existing_path(candidates)
    if discovered is not None:
        return discovered

    solver_on_path = shutil.which("sygus_solve")
    return Path(solver_on_path) if solver_on_path else None


def default_solver_command() -> str:
    solver_binary = discover_solver_binary()
    if solver_binary is not None:
        return f'"{solver_binary}" --json --no-cvc5-verify "{{input}}"'
    return 'sygus_solve --json --no-cvc5-verify "{input}"'


def discover_cvc5_binary() -> Path | None:
    root = project_root()
    candidates: list[Path] = []

    env_path = os.environ.get("CVC5_PATH")
    if env_path:
        candidates.append(Path(env_path))

    candidates.extend(
        [
            root / "dependencies" / "linux" / "install" / "bin" / "cvc5",
            root / "dependencies" / "ucrt64" / "install" / "bin" / "cvc5.exe",
            Path("D:/cvc5-Win64-static/bin/cvc5.exe"),
        ]
    )

    discovered = first_existing_path(candidates)
    if discovered is not None:
        return discovered

    cvc5_on_path = shutil.which("cvc5")
    return Path(cvc5_on_path) if cvc5_on_path else None


def default_cvc5_command(timeout_seconds: int) -> str:
    timeout_ms = timeout_seconds * 1000
    cvc5_binary = discover_cvc5_binary()
    if cvc5_binary is not None:
        return f'"{cvc5_binary}" --lang=sygus2 --tlimit={timeout_ms} "{{input}}"'
    return f'cvc5 --lang=sygus2 --tlimit={timeout_ms} "{{input}}"'


def discover_ucrt_runtime_dir() -> Path | None:
    candidates: list[Path] = []

    env_path = os.environ.get("SYGUS_UCRT_RUNTIME")
    if env_path:
        candidates.append(Path(env_path))

    candidates.extend(
        [
            Path("D:/msys64/ucrt64/bin"),
            Path("C:/msys64/ucrt64/bin"),
        ]
    )
    return first_existing_path(candidates)


def infer_runtime_dir(command_template: str) -> Path | None:
    normalized = command_template.lower()
    if os.name != "nt":
        return None
    if "ucrt" not in normalized and "build-ucrt" not in normalized:
        return None
    return discover_ucrt_runtime_dir()


def command_env(runtime_dir: Path | None) -> dict[str, str] | None:
    if runtime_dir is None:
        return None
    env = os.environ.copy()
    existing_path = env.get("PATH", "")
    env["PATH"] = str(runtime_dir) if not existing_path else f"{runtime_dir}{os.pathsep}{existing_path}"
    return env


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
    template: str,
    benchmark: Path,
    timeout_seconds: int,
    parse_json: bool = False,
    env: dict[str, str] | None = None,
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
                env=env,
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


def safe_relative_path(root: Path, benchmark: Path) -> str:
    try:
        return str(benchmark.relative_to(root))
    except ValueError:
        return str(benchmark)


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
                "cvc5_status_counts": {},
            },
        )
        bucket["benchmark_count"] += 1
        increment(bucket["solver_status_counts"], entry["solver"]["status"])
        increment(bucket["cvc5_status_counts"], entry["cvc5"]["status"])
    return summary


def build_outcome_summary(results: list[dict[str, Any]]) -> dict[str, int]:
    summary: dict[str, int] = {}
    for entry in results:
        solver_status = entry["solver"]["status"]
        cvc5_status = entry["cvc5"]["status"]
        if solver_status == "ok" and cvc5_status == "ok":
            key = "both_ok"
        elif solver_status == "ok":
            key = "solver_only_ok"
        elif cvc5_status == "ok":
            key = "cvc5_only_ok"
        elif solver_status == "timeout" and cvc5_status == "timeout":
            key = "both_timeout"
        else:
            key = "neither_ok"
        increment(summary, key)
    return summary


def benchmark_result(
    benchmark_root_str: str,
    benchmark_str: str,
    solver_command: str,
    cvc5_command: str,
    timeout_seconds: int,
    solver_runtime_dir_str: str | None,
) -> dict[str, Any]:
    benchmark_root = Path(benchmark_root_str)
    benchmark = Path(benchmark_str)
    track = track_name(benchmark_root, benchmark)
    solver_env = command_env(Path(solver_runtime_dir_str)) if solver_runtime_dir_str else None

    try:
        solver_result = run_command(
            solver_command,
            benchmark,
            timeout_seconds,
            parse_json=True,
            env=solver_env,
        )
        cvc5_result = run_command(cvc5_command, benchmark, timeout_seconds)
    except Exception as error:  # pragma: no cover - defensive worker guard
        command_error = str(error)
        solver_result = CommandResult(
            status="runner_error",
            exit_code=None,
            duration_ms=0,
            stdout="",
            stderr=command_error,
            command=solver_command.format(input=benchmark_str),
        )
        cvc5_result = CommandResult(
            status="runner_error",
            exit_code=None,
            duration_ms=0,
            stdout="",
            stderr=command_error,
            command=cvc5_command.format(input=benchmark_str),
        )

    return {
        "benchmark": str(benchmark),
        "relative_path": safe_relative_path(benchmark_root, benchmark),
        "track": track,
        "solver": asdict(solver_result),
        "cvc5": asdict(cvc5_result),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the local SyGuS solver and cvc5 over benchmark trees."
    )
    parser.add_argument("benchmark_root", help="Benchmark root or SyGuS benchmark repo root")
    parser.add_argument("--year", help="Competition year to run, e.g. 2019")
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Per-command timeout in seconds for both the local solver and cvc5",
    )
    parser.add_argument(
        "--workers",
        "--processes",
        dest="workers",
        type=int,
        default=DEFAULT_WORKERS,
        help="Parallel worker process count",
    )
    parser.add_argument(
        "--solver-command",
        default=None,
        help='Command template for the local solver, e.g. \'"build-ucrt/sygus_solve.exe --json --no-cvc5-verify {input}"\'',
    )
    parser.add_argument(
        "--cvc5-command",
        "--baseline-command",
        dest="cvc5_command",
        default=None,
        help='Command template for cvc5, e.g. \'"cvc5 --lang=sygus2 --tlimit=5000 {input}"\'',
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()

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

    worker_count = max(1, min(args.workers, len(benchmarks)))
    solver_command = args.solver_command or default_solver_command()
    cvc5_command = args.cvc5_command or default_cvc5_command(args.timeout)
    solver_runtime_dir = infer_runtime_dir(solver_command)

    print(f"Benchmark root: {benchmark_root}")
    print(f"Benchmarks found: {len(benchmarks)}")
    print(f"Workers: {worker_count}")
    print(f"Timeout (seconds): {args.timeout}")
    print(f"Solver command: {solver_command}")
    print(f"cvc5 command: {cvc5_command}")
    if solver_runtime_dir is not None:
        print(f"Solver runtime dir: {solver_runtime_dir}")

    results_by_index: list[dict[str, Any] | None] = [None] * len(benchmarks)
    solver_status_counts: dict[str, int] = {}
    cvc5_status_counts: dict[str, int] = {}

    with ProcessPoolExecutor(max_workers=worker_count) as executor:
        future_to_job = {
            executor.submit(
                benchmark_result,
                str(benchmark_root),
                str(benchmark),
                solver_command,
                cvc5_command,
                args.timeout,
                str(solver_runtime_dir) if solver_runtime_dir else None,
            ): (index, benchmark)
            for index, benchmark in enumerate(benchmarks, start=1)
        }

        completed_jobs = 0
        for future in as_completed(future_to_job):
            index, benchmark = future_to_job[future]
            entry = future.result()
            results_by_index[index - 1] = entry

            increment(solver_status_counts, entry["solver"]["status"])
            increment(cvc5_status_counts, entry["cvc5"]["status"])

            completed_jobs += 1
            print(
                f"[{completed_jobs}/{len(benchmarks)}] "
                f"track={entry['track']} "
                f"solver={entry['solver']['status']} "
                f"cvc5={entry['cvc5']['status']} "
                f"file={benchmark}"
            )

    results = [entry for entry in results_by_index if entry is not None]

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
        "worker_count": worker_count,
        "timeout_seconds": args.timeout,
        "solver_command": solver_command,
        "cvc5_command": cvc5_command,
        "solver_runtime_dir": str(solver_runtime_dir) if solver_runtime_dir else None,
        "benchmark_count": len(benchmarks),
        "solver_status_counts": solver_status_counts,
        "cvc5_status_counts": cvc5_status_counts,
        "outcome_summary": build_outcome_summary(results),
        "track_summary": build_track_summary(results),
        "results": results,
    }
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(f"Solver statuses: {solver_status_counts}")
    print(f"cvc5 statuses: {cvc5_status_counts}")
    print(f"Outcome summary: {payload['outcome_summary']}")
    print(f"Saved results to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

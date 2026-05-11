"""Extract training data from cvc5 SyGuS candidate traces.

Runs cvc5 with -o sygus --sygus-si=none to force enumeration mode and capture
intermediate candidates. Each candidate is labeled:
  1 = correct solution (final output from cvc5)
  0 = rejected candidate (intermediate, failed verification)

Usage:
    python scripts/extract_training_data.py <benchmark-dir> \
        --year 2019 --timeout 10 --workers 8 \
        --output training-data.jsonl
"""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any

DEFAULT_CVC5 = r"D:\cvc5-Win64-static\bin\cvc5.exe"
DEFAULT_TIMEOUT = 10
DEFAULT_WORKERS = 8
BENCHMARK_SUFFIXES = (".sl",)

RE_SYGUS_CANDIDATE = re.compile(r"^\(sygus-candidate (.+)\)$")
RE_DEFINE_FUN = re.compile(r"\(define-fun\s+")


@dataclass
class TrainingSample:
    benchmark: str
    candidate: str
    label: int
    candidate_index: int


def find_benchmarks(root: Path, year: str | None, max_files: int) -> list[Path]:
    search_root = root / "comp" / year if year else root
    if not search_root.is_dir():
        print(f"Directory not found: {search_root}", file=sys.stderr)
        return []

    files = sorted(
        p
        for p in search_root.rglob("*")
        if p.suffix in BENCHMARK_SUFFIXES and p.is_file()
    )
    if max_files > 0:
        files = files[:max_files]
    return files


def _run_cvc5(
    args: list[str],
    timeout_seconds: int,
) -> tuple[int, str, str]:
    popen_kwargs: dict[str, Any] = dict(
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if sys.platform == "win32":
        popen_kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        popen_kwargs["start_new_session"] = True

    proc = subprocess.Popen(args, **popen_kwargs)
    try:
        stdout, stderr = proc.communicate(timeout=timeout_seconds + 5)
    except subprocess.TimeoutExpired:
        if sys.platform == "win32":
            proc.kill()
        else:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        stdout, stderr = proc.communicate()

    rc = proc.returncode if proc.returncode is not None else -1
    return rc, stdout or "", stderr or ""


def _parse_candidates(stdout: str) -> list[str]:
    candidates = []
    for line in stdout.strip().splitlines():
        m = RE_SYGUS_CANDIDATE.match(line.strip())
        if m:
            candidates.append(m.group(1))
    return candidates


def _parse_solution(stdout: str) -> str | None:
    text = stdout.strip()
    if not text:
        return None
    idx = text.find("(define-fun ")
    if idx < 0:
        return None
    depth = 0
    end = idx
    for i in range(idx, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    return text[idx:end]


def extract_from_benchmark(
    benchmark: Path,
    cvc5_binary: str,
    timeout_seconds: int,
    root: Path,
) -> list[TrainingSample]:
    samples: list[TrainingSample] = []
    try:
        relative = str(benchmark.relative_to(root))
    except ValueError:
        relative = str(benchmark)

    tlimit = f"--tlimit={timeout_seconds * 1000}"

    # Pass 1: run cvc5 normally to get the solution (positive sample).
    rc_normal, stdout_normal, _ = _run_cvc5(
        [cvc5_binary, tlimit, str(benchmark)],
        timeout_seconds,
    )
    solution = _parse_solution(stdout_normal) if rc_normal == 0 else None

    # Pass 2: run with -o sygus --sygus-si=none to get enumeration trace.
    rc_enum, stdout_enum, _ = _run_cvc5(
        [cvc5_binary, "-o", "sygus", "--sygus-si=none", tlimit, str(benchmark)],
        timeout_seconds,
    )
    candidates = _parse_candidates(stdout_enum)

    # Also collect candidates from the enum run's solution if it solved.
    if rc_enum == 0:
        enum_solution = _parse_solution(stdout_enum)
        if enum_solution and not solution:
            solution = enum_solution

    for idx, cand in enumerate(candidates):
        samples.append(TrainingSample(
            benchmark=relative,
            candidate=cand,
            label=0,
            candidate_index=idx,
        ))

    if solution:
        samples.append(TrainingSample(
            benchmark=relative,
            candidate=solution,
            label=1,
            candidate_index=len(candidates),
        ))

    return samples


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract SyGuS training data from cvc5 traces")
    parser.add_argument("benchmark_root", type=Path)
    parser.add_argument("--year", default=None)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS)
    parser.add_argument("--max-benchmarks", type=int, default=0)
    parser.add_argument("--cvc5", default=DEFAULT_CVC5)
    parser.add_argument("--output", "-o", type=Path, default=Path("training-data.jsonl"))
    args = parser.parse_args()

    benchmarks = find_benchmarks(args.benchmark_root, args.year, args.max_benchmarks)
    if not benchmarks:
        print("No benchmarks found.", file=sys.stderr)
        return

    print(f"Benchmarks: {len(benchmarks)}")
    print(f"Workers: {args.workers}")
    print(f"Timeout: {args.timeout}s")
    print(f"cvc5: {args.cvc5}")
    print(f"Output: {args.output}")

    total_samples = 0
    total_positive = 0
    total_negative = 0
    processed = 0

    args.output.parent.mkdir(parents=True, exist_ok=True)

    worker_count = max(1, min(args.workers, len(benchmarks)))

    with open(args.output, "w", encoding="utf-8") as out_file:
        with ProcessPoolExecutor(max_workers=worker_count) as executor:
            futures = {
                executor.submit(
                    extract_from_benchmark,
                    bench,
                    args.cvc5,
                    args.timeout,
                    args.benchmark_root,
                ): bench
                for bench in benchmarks
            }

            for future in as_completed(futures):
                processed += 1
                bench = futures[future]
                try:
                    samples = future.result()
                except Exception as e:
                    print(f"[{processed}/{len(benchmarks)}] ERROR {bench}: {e}", file=sys.stderr)
                    continue

                pos = sum(1 for s in samples if s.label == 1)
                neg = sum(1 for s in samples if s.label == 0)
                total_samples += len(samples)
                total_positive += pos
                total_negative += neg

                for sample in samples:
                    json.dump({
                        "benchmark": sample.benchmark,
                        "candidate": sample.candidate,
                        "label": sample.label,
                        "candidate_index": sample.candidate_index,
                    }, out_file)
                    out_file.write("\n")

                status = "solved" if pos > 0 else ("candidates" if neg > 0 else "empty")
                if processed % 50 == 0 or processed == len(benchmarks):
                    print(f"[{processed}/{len(benchmarks)}] {status} samples={len(samples)} file={bench.name}")

    print(f"\nDone. Total samples: {total_samples} (positive={total_positive}, negative={total_negative})")
    print(f"Output: {args.output}")


if __name__ == "__main__":
    main()

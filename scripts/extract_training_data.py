"""Extract training data from SyGuS solver traces.

Uses CVC4 1.8 (--lang=sygus1) to solve benchmarks and extract correct
solutions (label=1).  Uses the local sygus_solve binary to enumerate
candidates that fail verification (label=0).

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

DEFAULT_CVC4 = r"D:\cvc4-1.8-win64-opt.exe"
DEFAULT_TIMEOUT = 10
DEFAULT_WORKERS = 8
BENCHMARK_SUFFIXES = (".sl",)

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


def _run_solver(
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


def _extract_define_fun_parts(solution: str) -> tuple[str, str, str, str]:
    """Extract (header, params_block, return_sort, body) from define-fun."""
    idx = solution.find("(define-fun ")
    if idx < 0:
        return ("", "", "", solution)

    tokens = solution[idx:].split()
    name = tokens[1] if len(tokens) > 1 else "f"

    paren_start = solution.find("((", idx)
    if paren_start < 0:
        return ("", "", "", solution)

    depth = 0
    params_end = paren_start
    for i in range(paren_start, len(solution)):
        if solution[i] == "(":
            depth += 1
        elif solution[i] == ")":
            depth -= 1
            if depth == 0:
                params_end = i + 1
                break

    params_block = solution[paren_start:params_end]

    rest = solution[params_end:].strip()
    ret_sort_end = 0
    if rest.startswith("("):
        depth = 0
        for i in range(len(rest)):
            if rest[i] == "(":
                depth += 1
            elif rest[i] == ")":
                depth -= 1
                if depth == 0:
                    ret_sort_end = i + 1
                    break
    else:
        ret_sort_end = rest.find(" ")
        if ret_sort_end < 0:
            ret_sort_end = len(rest)

    return_sort = rest[:ret_sort_end]
    body = rest[ret_sort_end:].strip().rstrip(")")
    header = f"(define-fun {name} {params_block} {return_sort} "

    return header, params_block, return_sort, body


def _extract_params(params_block: str) -> list[str]:
    """Extract parameter names from ((p1 sort1) (p2 sort2) ...)."""
    params = []
    depth = 0
    current = ""
    for c in params_block:
        if c == "(":
            depth += 1
            if depth == 2:
                current = ""
        elif c == ")":
            depth -= 1
        elif c == " " and depth == 2 and current:
            params.append(current)
            current = ""
        elif depth == 2:
            current += c
    return params


def _extract_subtrees(body: str) -> list[str]:
    """Extract balanced sub-expressions from body."""
    subtrees = []
    i = 0
    n = len(body)
    while i < n:
        if body[i] == "(":
            depth = 0
            start = i
            for j in range(i, n):
                if body[j] == "(":
                    depth += 1
                elif body[j] == ")":
                    depth -= 1
                    if depth == 0:
                        sub = body[start:j + 1]
                        if len(sub) < len(body) * 0.8:
                            subtrees.append(sub)
                        i = j + 1
                        break
            else:
                i += 1
        else:
            i += 1
    return subtrees


def _generate_negative_candidates(solution: str, count: int) -> list[str]:
    """Generate wrong candidates: subtrees, constants, operator swaps."""
    negatives: list[str] = []

    header, params_block, return_sort, body = _extract_define_fun_parts(solution)
    params = _extract_params(params_block)

    def _wrap(expr: str) -> str:
        if header:
            return f"{header}{expr})"
        return expr

    for p in params[:count]:
        negatives.append(_wrap(p))
        if len(negatives) >= count:
            return negatives

    for const in ["0", "1", "-1", "42"]:
        negatives.append(_wrap(const))
        if len(negatives) >= count:
            return negatives

    subtrees = _extract_subtrees(body)
    for sub in subtrees:
        negatives.append(_wrap(sub))
        if len(negatives) >= count:
            return negatives

    swaps = [
        ("+", "-"), ("-", "+"), ("*", "+"), ("and", "or"), ("or", "and"),
        ("<=", "<"), ("<", "<="), (">=", ">"), (">", ">="),
        ("bvand", "bvor"), ("bvor", "bvand"),
        ("bvadd", "bvsub"), ("bvsub", "bvadd"),
    ]
    for old_op, new_op in swaps:
        padded_old = f"({old_op} "
        if padded_old in body:
            neg = body.replace(padded_old, f"({new_op} ", 1)
            if neg != body:
                negatives.append(_wrap(neg))
                if len(negatives) >= count:
                    return negatives

    return negatives


def extract_from_benchmark(
    benchmark: Path,
    cvc4_binary: str,
    timeout_seconds: int,
    root: Path,
    max_neg_per_benchmark: int,
) -> list[TrainingSample]:
    samples: list[TrainingSample] = []
    try:
        relative = str(benchmark.relative_to(root))
    except ValueError:
        relative = str(benchmark)

    tlimit_ms = timeout_seconds * 1000

    # Pass 1: run CVC4 with --lang=sygus1 to get the solution (positive sample).
    rc_cvc4, stdout_cvc4, _ = _run_solver(
        [cvc4_binary, "--lang=sygus1", f"--tlimit={tlimit_ms}", str(benchmark)],
        timeout_seconds,
    )
    solution = _parse_solution(stdout_cvc4) if rc_cvc4 == 0 else None

    # Generate negative candidates by perturbing the solution.
    neg_candidates: list[str] = []
    if solution:
        neg_candidates = _generate_negative_candidates(solution, max_neg_per_benchmark)

    for idx, cand in enumerate(neg_candidates):
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
            candidate_index=len(neg_candidates),
        ))

    return samples


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract SyGuS training data")
    parser.add_argument("benchmark_root", type=Path)
    parser.add_argument("--year", default=None)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--workers", type=int, default=DEFAULT_WORKERS)
    parser.add_argument("--max-benchmarks", type=int, default=0)
    parser.add_argument("--max-neg-per-benchmark", type=int, default=50)
    parser.add_argument("--cvc4", default=DEFAULT_CVC4)
    parser.add_argument("--output", "-o", type=Path, default=Path("training-data.jsonl"))
    args = parser.parse_args()

    benchmarks = find_benchmarks(args.benchmark_root, args.year, args.max_benchmarks)
    if not benchmarks:
        print("No benchmarks found.", file=sys.stderr)
        return

    print(f"Benchmarks: {len(benchmarks)}")
    print(f"Workers: {args.workers}")
    print(f"Timeout: {args.timeout}s")
    print(f"CVC4: {args.cvc4}")
    print(f"Max negatives/benchmark: {args.max_neg_per_benchmark}")
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
                    args.cvc4,
                    args.timeout,
                    args.benchmark_root,
                    args.max_neg_per_benchmark,
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

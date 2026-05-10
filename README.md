# SyGuS Generator

This repository is being reshaped around a parser-first workflow so we can
reliably ingest SyGuS benchmark programs before tightening the solver side.

## Current focus

- Parse SyGuS programs into a structured AST
- Validate parsing with unit tests (`sygus_ut`) and module tests (`sygus_mt`)
- Compare parser coverage against `cvc5 --parse-only` on benchmark trees

## Build

Example UCRT/Ninja configure:

```powershell
D:\msys64\ucrt64\bin\cmake.exe -S . -B build `
  -G Ninja `
  -D CMAKE_CXX_COMPILER=D:\msys64\ucrt64\bin\g++.exe
```

Build and test:

```powershell
D:\msys64\ucrt64\bin\cmake.exe --build build
ctest --test-dir build --output-on-failure
```

Helper script for the common MSYS2 UCRT flow:

```bash
./scripts/build_ucrt.sh
```

Useful overrides:

```bash
BUILD_TYPE=Release CLEAN_BUILD=1 ./scripts/build_ucrt.sh
STAGE_THIRD_PARTY=0 ./scripts/build_ucrt.sh -DSYGUS_ENABLE_COVERAGE=ON
```

## Main targets

- `sygus_parser` - parser library
- `sygus_parse` - parser CLI
- `sygus_ut` - parser unit tests
- `sygus_mt` - parser module tests

## Formatting

Format the parser-owned C++ files:

```bash
./scripts/format_code.sh
```

Check formatting without modifying files:

```bash
./scripts/check_clang_format.sh
```

## Benchmark workflow

Stage sources into `third-party/`:

```bash
./scripts/stage_third_party_sources_ucrt.sh
```

Inspect competition-year coverage:

```bash
./scripts/inspect_sygus_benchmarks.sh
./scripts/inspect_sygus_benchmarks.sh 2019
```

Compare the local parser against `cvc5 --parse-only`:

```powershell
python scripts/run_sygus_benchmarks.py third-party/sygus-benchmarks --year 2019
```

## Planned Dependency Flow

The next dependency step should mirror the `trading-system` pattern, but for
SyGuS solver libraries:

- local source staging stays under `third-party/` via `scripts/stage_third_party_sources_ucrt.sh`
- local built dependency outputs should live under `dependencies/ucrt64/install`
- CI built dependency outputs should live under `dependencies/linux/install`
- CI should restore a prebuilt Linux dependency archive from a GitHub release
  asset during normal builds, and only rebuild that asset on a special trigger
  push
- the explicit trigger for the one-time or refresh release build should be a
  commit message containing `%BUILD_RELEASE`

Concretely, that points to these next scripts/jobs:

- `scripts/build_third_party_dependencies_ucrt.sh`
  - build cvc5 and any required support libraries into `dependencies/ucrt64/install`
- `scripts/rebuild_linux_deps_ci.sh`
  - build the matching Linux dependency prefix and archive it for CI reuse
- `.github/workflows/ci.yml`
  - normal `build-and-test` and `coverage` jobs restore the release asset
  - a dedicated release job rebuilds and uploads it only when `%BUILD_RELEASE`
    appears in the pushed commit message

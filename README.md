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

## Main targets

- `sygus_parser` - parser library
- `sygus_parse` - parser CLI
- `sygus_ut` - parser unit tests
- `sygus_mt` - parser module tests

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

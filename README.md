# SyGuS Generator

This repository is being turned into a real SyGuS workflow around three layers:

- a structured SyGuS parser
- a small AST-based enumerative solver
- reusable cvc5 dependency bundles for local builds and CI

## Current scope

The project currently supports:

- parsing SyGuS programs into an AST
- solving a focused fragment of single `synth-fun` benchmarks
- exact verification through cvc5 when the dependency bundle is available
- sample-validated solving when cvc5 is not built into the current tree

The first supported synthesis fragment is intentionally narrow:

- `LIA` single-function synthesis
- small `BV` grammars with basic bit-vector operators

Not supported yet:

- invariant synthesis
- multi-function SyGuS problems
- broad benchmark-track coverage across every yearly competition category

## Main targets

- `sygus_parser`
  - parser library
- `sygus_parse`
  - parser CLI
- `sygus_solver`
  - AST-based enumerative solver library
- `sygus_solve`
  - solver CLI
- `sygus_ut`
  - unit-style parser/solver tests
- `sygus_mt`
  - module-style parser/solver tests

## Local build

Fast local UCRT build without rebuilding dependencies:

```bash
./scripts/build_ucrt.sh
```

Build local dependencies first, then require cvc5 during configure:

```bash
BUILD_DEPENDENCIES=1 CLEAN_BUILD=1 ./scripts/build_ucrt.sh
```

Useful overrides:

```bash
BUILD_TYPE=Release CLEAN_BUILD=1 ./scripts/build_ucrt.sh
STAGE_THIRD_PARTY=0 ./scripts/build_ucrt.sh
```

The helper script automatically uses `dependencies/ucrt64/install` as
`CMAKE_PREFIX_PATH` when that prefix exists.

## Dependency scripts

Stage source trees under `third-party/`:

```bash
./scripts/stage_third_party_sources_ucrt.sh
```

Build the reusable local UCRT cvc5 dependency prefix:

```bash
./scripts/build_third_party_dependencies_ucrt.sh
```

Build the reusable Linux CI dependency bundle:

```bash
./scripts/rebuild_linux_deps_ci.sh
```

Outputs:

- local install prefix:
  - `dependencies/ucrt64/install`
- Linux CI install prefix:
  - `dependencies/linux/install`
- Linux CI archive:
  - `dependencies/linux/sygus-linux-deps-ubuntu-latest.tar.gz`

## CI dependency bundle flow

The CI workflow mirrors the `trading-system` release-asset pattern:

- normal CI jobs download `sygus-linux-deps-ubuntu-latest.tar.gz` from the
  GitHub release `sygus-linux-deps`
- normal CI jobs extract that archive and build/link against
  `dependencies/linux/install`
- dependency bundle rebuilds are triggered only by pushes whose commit message
  contains `%BUILD_RELEASE`

That means the intended bootstrap flow is:

1. push a commit containing `%BUILD_RELEASE`
2. let the `linux-deps-release` job publish the first bundle asset
3. run normal CI jobs against that reusable bundle afterward

## Formatting

Format the parser/solver-owned C++ files:

```bash
./scripts/format_code.sh
```

Check formatting without modifying files:

```bash
./scripts/check_clang_format.sh
```

## Coverage

Run local coverage:

```bash
./scripts/run_coverage_ci.sh
```

If `dependencies/linux/install` exists, the script automatically uses it as
`CMAKE_PREFIX_PATH`.

## CLI usage

Parse a SyGuS file:

```bash
./build-ucrt/sygus_parse tests/data/lia_max2.sl
```

Solve a SyGuS file:

```bash
./build-ucrt/sygus_solve --json tests/data/lia_max2.sl
```

When cvc5 is linked into the build, `sygus_solve` verifies candidates exactly.
Without cvc5, it still returns the best sample-validated candidate and marks
that in the output.

## Benchmark workflow

Inspect yearly SyGuS competition coverage:

```bash
./scripts/inspect_sygus_benchmarks.sh
./scripts/inspect_sygus_benchmarks.sh 2019
```

Run the local solver against benchmarks and compare with cvc5:

```powershell
python scripts/run_sygus_benchmarks.py third-party/sygus-benchmarks --year 2019
```

Useful benchmarking options:

```powershell
python scripts/run_sygus_benchmarks.py third-party/sygus-benchmarks --year 2019 --max-benchmarks 25
python scripts/run_sygus_benchmarks.py tests/data --timeout 10
```

The benchmark runner now:

- runs `sygus_solve --json` by default
- runs real cvc5 solving as the baseline
- records solver status by benchmark
- summarizes counts by track/category

## Suggested next steps

The most useful next engineering steps after this pass are:

1. expand exact cvc5-backed verification and translation coverage for more
   SyGuS operators
2. extend the enumerator beyond the current LIA/BV starter fragment
3. add a benchmark subset policy per competition year so we can measure
   progress consistently
4. connect solver outputs to a richer results/analysis layer for comparing with
   cvc5 over time

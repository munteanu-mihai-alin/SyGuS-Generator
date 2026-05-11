## [2026-05-04] - Build parser-first SyGuS project structure

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository at commit `5b8ead8`

User request:
- Add repo-local ignore/build hygiene, inspect `D:\trading-system` formatter and CI patterns, inspect `D:\PythonSynth` benchmark runner scripts, and turn this repo into a parser-first SyGuS project with AST parsing, parser tests, CI, coverage, and local benchmark-runner hooks.

Files changed:
- `CMakeLists.txt` - replaced the machine-local CMake setup with a parser-first build that produces `sygus_parser`, `sygus_parse`, and explicit `ut` / `mt` targets
- `.gitignore` - added build/CMake/result/artifact ignore rules
- `.clang-format` - copied in the fuller formatter configuration shape used in `D:\trading-system`
- `README.md` - documented build, test, benchmark, and parser CLI workflows
- `include/sygus_ast.hpp` - added a structured AST and compatibility fields for legacy parser consumers
- `include/invariant.h` - converted to a compatibility include for the new AST types
- `include/sygus_parser.hpp` - replaced the stateful token walker API with a parser-first API that supports string, file, and raw-form parsing
- `src/sygus_parser.cpp` - rewrote parsing around S-expressions and structured SyGuS commands
- `src/sygus_parse_main.cpp` - added a parser CLI with summary, dump, and parse-only modes
- `tests/test_harness.hpp` - added a tiny no-mocks test harness
- `tests/sygus_ut.cpp` - added unit tests for forms, comments, primed vars, and grammar conversion
- `tests/sygus_mt.cpp` - added end-to-end module tests for LIA, BV, and invariant-style inputs
- `tests/data/lia_max2.sl` - parser module test input
- `tests/data/bv_double.sl` - parser module test input
- `tests/data/invariant_with_primed.sl` - parser module test input
- `scripts/check_clang_format.sh` - added a parser-owned formatting gate for CI
- `scripts/run_coverage_ci.sh` - added Linux coverage automation for the parser targets
- `scripts/run_sygus_benchmarks.py` - added a repo-local benchmark runner inspired by the useful scan/run/report flow from `D:\PythonSynth\main.py`
- `.github/workflows/ci.yml` - added parser-first CI and coverage jobs
- `AGENT_HANDOFF_LOG.md` - recorded this interaction

Deletions / removals:
- Removed the old machine-local `CMakeLists.txt` contents that hardcoded `D:\...` cvc5 paths into the default build
- Removed the old minimal `.clang-format` contents
- Replaced the old ad hoc parser implementation in `src/sygus_parser.cpp`

Steps taken:
1. Inspected the existing repo layout, parser sources, and build files.
2. Compared the repo against `D:\trading-system` to reuse the stronger formatter and the general CI shape without copying that project wholesale.
3. Inspected `D:\PythonSynth\main.py` and reused only its useful benchmark ideas: recursive benchmark discovery, exclude lists, per-file command execution, and result collection.
4. Replaced the parser with an S-expression-based frontend that handles comments, quoted forms, structured commands, grammar rules, primed vars, and invariant commands.
5. Split parsing into a standalone library/CLI and added parser-focused unit and module tests.
6. Added repo-local CI, coverage, and benchmark-runner scripts that stay contained to this project directory.
7. Configured and built with `D:\msys64\ucrt64\bin\cmake.exe` and `D:\msys64\ucrt64\bin\g++.exe`, then ran tests and a benchmark-runner self-check.

Validation performed:
- `D:\msys64\ucrt64\bin\cmake.exe -S . -B build -G Ninja -D CMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe -D CMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/ninja.exe -D SYGUS_BUILD_TESTS=ON`
- `D:\msys64\ucrt64\bin\cmake.exe --build build -j 4`
- `ctest --test-dir build --output-on-failure`
- `build\sygus_parse.exe tests\data\lia_max2.sl`
- `python -m py_compile scripts\run_sygus_benchmarks.py`
- `python scripts\run_sygus_benchmarks.py tests\data --solver-command "build\sygus_parse.exe --parse-only {input}" --baseline-command "build\sygus_parse.exe --parse-only {input}" --output results\benchmark-runs\self-check.json`
- Manual `clang-format` equivalence check over the parser-owned C++ files

Known risks / follow-up:
- The legacy synthesis sources are still in the repository and still need a later compatibility pass against the new AST if we want them back in the default build.
- The new CI format gate intentionally checks only the parser-owned files introduced in this pass, not the entire legacy source tree, because unrelated older files still have formatting debt and trailing whitespace.
- The benchmark runner currently compares parse behavior cleanly; solver-quality comparisons against `cvc5` will need a later project-specific solver command once the synthesis path is rebuilt.

Suggested commit:
```bash
git commit -m "feat(parser): add parser-first sygus frontend and test harness"
```

## [2026-05-04] - Remove stale CMake build directories

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository at commit `5b8ead8`

User request:
- Remove all `cmake-build-*` directories from `D:\SyGuS-Generator`.

Files changed:
- `AGENT_HANDOFF_LOG.md` - recorded removal of stale build directories after a permissions retry

Deletions / removals:
- Removed `D:\SyGuS-Generator\cmake-build-debug`
- Removed `D:\SyGuS-Generator\cmake-build-debug-ucrt`
- Removed `D:\SyGuS-Generator\cmake-build-debug-wsl`
- Removed `D:\SyGuS-Generator\cmake-build-release-ucrt`
- Removed `D:\SyGuS-Generator\cmake-build-release-wsl`
- Removed `D:\SyGuS-Generator\cmake-build-release-wsl-1`

Steps taken:
1. Enumerated all `cmake-build-*` directories and verified their resolved paths stayed under `D:\SyGuS-Generator`.
2. Attempted a normal PowerShell `Remove-Item -Recurse -Force` pass.
3. Retried the same verified removal with elevated filesystem access after Windows access-denied errors blocked the first pass.

Validation performed:
- Verified the exact target directory paths before deletion
- Elevated deletion completed successfully

Known risks / follow-up:
- none

Suggested commit:
```bash
git commit -m "chore: clean stale cmake build directories"
```

## [2026-05-04] - Add UCRT third-party staging scripts

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository at commit `5b8ead8`

User request:
- Add a UCRT-friendly script flow for downloading cvc5 source code and SyGuS competition benchmarks into `third-party/`, with archives staged under `third-party/_downloads`, plus a helper to inspect benchmark years.

Files changed:
- `scripts/stage_third_party_sources_ucrt.sh` - added a repo-local staging script that downloads cvc5 and SyGuS benchmarks into `third-party/_downloads` and unpacks them into `third-party/`
- `scripts/inspect_sygus_benchmarks.sh` - added a helper that summarizes available SyGuS competition years and per-year group counts
- `third-party/.gitignore` - keeps staged third-party trees out of Git by default while preserving the directory
- `third-party/_downloads/.gitignore` - keeps temporary download artifacts out of Git
- `AGENT_HANDOFF_LOG.md` - created the handoff log and recorded this interaction

Deletions / removals:
- none

Steps taken:
1. Inspected the current repository layout, CMake usage of cvc5, and the copied agent workflow notes.
2. Reused the general staging shape from the other project but adapted it to this repository’s requested `third-party/` layout.
3. Pinned the default cvc5 source download to `cvc5-1.2.1` to match the repo’s existing local cvc5 artifacts and deprecated API usage, while still allowing overrides through environment variables.
4. Added a benchmark inspection helper for the downloaded `SyGuS-Org/benchmarks` tree.

Validation performed:
- File inspection of current CMake and local cvc5 include drop to choose a compatible default version
- Read-back inspection of `scripts/stage_third_party_sources_ucrt.sh` and `scripts/inspect_sygus_benchmarks.sh`
- Attempted local bash validation via WSL bash and Git bash, but both failed in this environment with Win32 access-denied / file-mapping errors before script execution

Known risks / follow-up:
- The scripts are not wired into `CMakeLists.txt` yet; they stage sources only.
- Network download paths were chosen from official upstream sources, but full end-to-end download/build validation still depends on running the scripts in an MSYS2 UCRT64 shell.

Suggested commit:
```bash
git commit -m "build(third-party): add ucrt source staging scripts"
```

## [2026-05-10] - Add format and local build helpers

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after the parser-first setup commit

User request:
- Add a format script similar to the one in `D:\trading-system`.
- Add a local build helper mirroring the other project's day-to-day workflow.
- Write down the next-step plan for dependency staging, release assets, and a real SyGuS project flow.

Files changed:
- `.gitignore` - now ignores Python bytecode caches and future `dependencies/` build outputs
- `scripts/format_code.sh` - formats the parser-owned C++ files with the repo's `.clang-format`
- `scripts/build_ucrt.sh` - stages third-party sources, configures a UCRT build, builds targets, runs tests, and performs a parser smoke check
- `README.md` - documents formatting, the new local build helper, and the planned dependency/release-asset workflow
- `AGENT_HANDOFF_LOG.md` - recorded this handoff entry

Deletions / removals:
- none

Steps taken:
1. Inspected `D:\trading-system`'s `format_code.sh`, `check_clang_format.sh`, and CI release-bundle pattern.
2. Added a matching `scripts/format_code.sh` to this repo, but intentionally scoped it to parser-owned files so it does not reformat the legacy solver files that are still being migrated.
3. Added `scripts/build_ucrt.sh` as a repo-local helper for the common stage-configure-build-test-smoke workflow.
4. Documented the next dependency steps needed to move from parser-first into a real solver project with reusable CI dependency bundles.

Validation performed:
- read-back inspection of the new script contents
- `bash -n scripts/format_code.sh`
- `bash -n scripts/build_ucrt.sh`
- `STAGE_THIRD_PARTY=0 CLEAN_BUILD=1 ./scripts/build_ucrt.sh`
  - configure/build/test/smoke succeeded under the local UCRT toolchain

Known risks / follow-up:
- `scripts/build_ucrt.sh` currently builds only the parser-first target set; it does not yet build a cvc5 dependency prefix because the solver target has not been reconnected to the new AST pipeline.
- The CI release-asset plan is documented but not yet implemented; the intended next step is to add `build_third_party_dependencies_ucrt.sh` and `rebuild_linux_deps_ci.sh` for this repository.

Suggested commit:
```bash
git commit -m "add format and ucrt build helpers"
```

## [2026-05-11] - Add solver path, dependency scripts, CI bundle flow, and benchmark upgrades

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after commit `c132c21`

User request:
- Do the next full repo step:
  - add local and CI dependency-bundle scripts
  - reconnect the project to a real AST-based solver path
  - upgrade tests and benchmark runner
  - perform repo cleanup where safe

Files changed:
- `CMakeLists.txt` - added `sygus_solver` / `sygus_solve`, optional cvc5 package wiring, and solver-aware test linking
- `README.md` - rewritten around the real local build, dependency bundle, solver, benchmark, and CI flows
- `.gitignore` - expanded cleanup ignores for `agent/`, archived cvc5 drops, and root-level ad-hoc benchmark files
- `.github/workflows/ci.yml` - now restores a reusable Linux cvc5 bundle, runs solver-aware CI, and rebuilds the release asset only on `%BUILD_RELEASE`
- `include/sygus_solver.hpp` - added solver API and result/option structs
- `src/sygus_solver.cpp` - added AST-based bottom-up enumerative synthesis, sample evaluation, optional cvc5 verification, and unsupported-fragment handling
- `src/sygus_solve_main.cpp` - added solver CLI with JSON/text output
- `tests/sygus_ut.cpp` - added solver unit coverage for a simple synth-fun and invariant unsupported-path behavior
- `tests/sygus_mt.cpp` - added solver module coverage for `lia_max2`, `bv_double`, and invariant unsupported behavior
- `scripts/build_third_party_dependencies_ucrt.sh` - added local UCRT cvc5 dependency-prefix build script
- `scripts/rebuild_linux_deps_ci.sh` - added Linux cvc5 dependency-bundle builder for CI release assets
- `scripts/build_ucrt.sh` - now optionally builds dependencies, auto-uses the local cvc5 prefix, and smoke-runs `sygus_solve`
- `scripts/run_coverage_ci.sh` - now auto-uses the Linux dependency prefix when present
- `scripts/check_clang_format.sh` - now checks the new solver-owned files too
- `scripts/format_code.sh` - now formats the new solver-owned files too
- `scripts/run_sygus_benchmarks.py` - now runs `sygus_solve --json`, compares against real cvc5 command templates, and summarizes by track/status
- `AGENT_HANDOFF_LOG.md` - recorded this interaction

Deletions / removals:
- Removed temporary local probe directories used during dependency-build experiments
- Removed `scripts/__pycache__/`

Steps taken:
1. Inspected the new AST parser and the old prototype solver code, then chose to add a clean new solver path in separate repo-owned files rather than mutate the user-edited prototype files.
2. Implemented a bottom-up AST-based enumerative solver for the current narrow scope:
   - single `synth-fun`
   - current focus on `LIA` and small `BV` grammars
   - sample-driven synthesis with optional exact cvc5 verification
3. Hooked the solver into CMake with a `sygus_solve` CLI and kept the solver build optional on the presence of a cvc5 package/prefix.
4. Extended `ut` / `mt` coverage to include real solver behavior on benchmark-style inputs instead of only parser structure checks.
5. Added local and CI dependency scripts modeled on the `trading-system` asset-bundle flow, with `%BUILD_RELEASE` gating the release-asset rebuild path.
6. Reworked the benchmark runner so it now measures solver status and groups results by benchmark track/category.
7. Cleaned repo-local clutter by ignoring ad-hoc benchmark drops and old archived cvc5 directories, and by removing temporary validation directories.

Validation performed:
- `./scripts/format_code.sh` with the UCRT toolchain in PATH
- `./scripts/check_clang_format.sh`
- `bash -n scripts/build_third_party_dependencies_ucrt.sh`
- `bash -n scripts/rebuild_linux_deps_ci.sh`
- `python -m py_compile scripts/run_sygus_benchmarks.py`
- `STAGE_THIRD_PARTY=0 CLEAN_BUILD=1 ./scripts/build_ucrt.sh`
  - configure/build/tests/smoke succeeded
- `build-ucrt/sygus_solve.exe tests/data/lia_max2.sl --no-cvc5-verify --json`
  - solved with `(ite (<= x y) y x)`
- `build-ucrt/sygus_solve.exe tests/data/bv_double.sl --no-cvc5-verify --json`
  - solved with `(bvadd x x)`
- `python scripts/run_sygus_benchmarks.py tests/data --timeout 10 --max-benchmarks 3 --solver-command "...sygus_solve.exe --json {input}" --baseline-command "...sygus_solve.exe --json --no-cvc5-verify {input}" --output results/benchmark-runs/self-check.json`
  - solver self-check completed with `solved=2`, `unsupported=1`
- `DRY_RUN=1 ./scripts/build_third_party_dependencies_ucrt.sh`
- `DRY_RUN=1 ./scripts/rebuild_linux_deps_ci.sh`

Known risks / follow-up:
- The actual UCRT cvc5 dependency build still fails in this environment. The script now gets much further, but cvc5 still configures/builds with `clang` instead of the intended `gcc`, and the build then fails on:
  - `thread_local ... cannot be dllexport`
- The Linux dependency-bundle script is validated in dry-run mode only from this Windows environment. It still needs a real Linux runner pass.
- The current solver intentionally supports only a narrow starter fragment. It is useful for real project scaffolding and benchmark measurement, but it is not yet a broad SyGuS competition solver.

Suggested commit:
```bash
git commit -m "add solver and ci deps flow"
```

## [2026-05-11] - Refresh handoff state after benchmark-runner fix and Windows DLL investigation

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after commit `3492071`

User request:
- Keep the agent handoff log current after the recent benchmark-runner work and the direct `cvc5` / solver runs on real SyGuS competition benchmarks.

Files changed:
- `AGENT_HANDOFF_LOG.md` - recorded the current benchmark-runner state, the Windows DLL finding, and the remaining parser blocker

Deletions / removals:
- none

Steps taken:
1. Inspected the existing handoff log, current Git history, and worktree state to identify the missing project history.
2. Verified the current benchmark runner behavior from plain PowerShell using the real benchmark tree under `third-party/sygus-benchmarks`.
3. Confirmed that `cvc5` runs successfully from `D:\cvc5-Win64-static\bin\cvc5.exe`.
4. Confirmed that the UCRT-built local solver initially failed with Windows loader error `0xC0000135` when the MSYS2 UCRT runtime DLL directory was not on `PATH`.
5. Confirmed that the updated benchmark runner now injects `D:\msys64\ucrt64\bin` into child-process `PATH` automatically when it discovers `build-ucrt\sygus_solve.exe`, which removes the missing-DLL failure from benchmark runs.
6. Isolated the next real blocker after the DLL fix: the parser/solver still rejects a real SyGuS competition benchmark with the message `synth-fun expects four arguments`, while `cvc5` solves the same benchmark.

Validation performed:
- `python scripts/run_sygus_benchmarks.py third-party/sygus-benchmarks --year 2017 --max-benchmarks 1`
  - confirmed the runner executes both the local solver and `cvc5`
- direct `build-ucrt\sygus_solve.exe` invocation from plain PowerShell
  - reproduced `STATUS_DLL_NOT_FOUND` before the runtime-path injection fix
- direct `cvc5` invocation on:
  - `third-party/sygus-benchmarks/comp/2017/CLIA_Track/fg_array_search_10.sl`
  - confirmed `cvc5` returns a valid solution
- reran the benchmark runner after the runtime-path fix
  - confirmed the missing-DLL issue is gone and the remaining failure is a real parser/solver error, not a Windows loader crash

Known risks / follow-up:
- The benchmark runner is now in a usable state for bulk measurement, but the solver still cannot parse at least some real competition benchmarks that use the short `synth-fun` form without an explicit grammar.
- The next concrete code fix should be in `src/sygus_parser.cpp`, so that real competition benchmarks like `fg_array_search_10.sl` parse successfully before further solver-vs-`cvc5` evaluation.
- Local IDE files remain user-local and were intentionally not folded into project history:
  - `.idea/.name`
  - `.idea/editor.xml`

Suggested commit:
```bash
git commit -m "docs: refresh handoff log"
```

## [2026-05-11] - Move handoff log under agent/

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after commit `3492071`

User request:
- Move the handoff log under `agent/` so it lives with the rest of the agent workflow files.

Files changed:
- `agent/AGENT_HANDOFF_LOG.md` - moved the handoff log from the repository root into `agent/`
- `.gitignore` - replaced the blanket `agent/` ignore with narrow rules that still track the agent workflow files and the handoff log

Deletions / removals:
- Removed the old root path `AGENT_HANDOFF_LOG.md`

Steps taken:
1. Inspected current references to the handoff log and the existing `agent/` ignore rules.
2. Moved the handoff log into `agent/` beside `AGENT_WORKFLOW.md`.
3. Updated `.gitignore` so `agent/AGENT_HANDOFF_LOG.md` remains a normal tracked project file.

Validation performed:
- Verified the new path lives under `D:\SyGuS-Generator\agent`
- Verified the old root path is no longer the source of truth

Known risks / follow-up:
- Any future references should use `agent/AGENT_HANDOFF_LOG.md` rather than the old root path.

Suggested commit:
```bash
git commit -m "move handoff log"
```

## [2026-05-11] - Download SyGuS-IF specs and pivot parser work to benchmark-only inputs

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after commit `3492071`

User request:
- Use only real benchmark files to drive parser updates.
- Download the SyGuS-IF specification files for each version into the project.
- Summarize the next work items.

Files changed:
- `third-party/sygus-if-specs/SyGuS-IF_1.0_2014.pdf` - downloaded official original SyGuS-IF proposal
- `third-party/sygus-if-specs/SyGuS-IF_2015_CLIA_INV.pdf` - downloaded official 2015 CLIA / INV extension
- `third-party/sygus-if-specs/SyGuS-IF_2016_PBE.pdf` - downloaded official 2016 PBE extension
- `third-party/sygus-if-specs/SyGuS-IF_2.0_2019.pdf` - downloaded official SyGuS-IF 2.0 spec
- `third-party/sygus-if-specs/SyGuS-IF_2.1_2021.pdf` - downloaded official SyGuS-IF 2.1 spec
- `third-party/sygus-if-specs/README.md` - documented source URLs and mapped the versions to the benchmark syntax used in this repo

Deletions / removals:
- none

Steps taken:
1. Located the official SyGuS language pages and spec PDFs from `sygus-org.github.io`.
2. Downloaded the core versioned specs into `third-party/sygus-if-specs/`.
3. Included the 2015 and 2016 extension specs as well, since older competition inputs can still rely on those forms.
4. Scanned the currently staged benchmark tree under `third-party/sygus-benchmarks/comp` to decide the parser order of attack based on real inputs.

Validation performed:
- Verified the downloaded spec files exist under `third-party/sygus-if-specs/`
- Scanned `third-party/sygus-benchmarks/comp` and confirmed the available years are:
  - `2017`
  - `2018`
  - `2019`
- Scanned benchmark files and confirmed heavy usage of:
  - short `synth-fun` declarations without explicit grammars
  - `synth-inv`
  - `declare-primed-var`
  - `inv-constraint`
  - old bit-vector sort syntax `(BitVec n)`
  - legacy grammar symbols `Start`, `StartBool`, `Constant`, `Variable`
- Scanned benchmark files and found no current use of:
  - `assume`
  - oracle commands
  - `chc-constraint`
  - `optimize-synth`

Known risks / follow-up:
- The current parser/solver still fails on at least one real 2017 CLIA benchmark because it rejects the short `synth-fun` form without an explicit grammar.
- The next parser work should stay benchmark-first and fix only syntax/features that are observed in the staged benchmark corpus before expanding to newer optional language features.

Suggested commit:
```bash
git commit -m "add sygus-if specs"
```

## [2026-05-11] - Parser now accepts the staged 2019 competition corpus

Model / agent:
- GPT-5.5 Thinking, reasoning model

Source state:
- Local repository on `main` after commit `429ab34`

User request:
- Investigate the current CI failure.
- Start extending the parser using only real benchmark-file syntax so it can eventually parse every file in the staged `comp/2019` setup.

Files changed:
- `src/sygus_parser.cpp` - extended `synth-fun` parsing to accept:
  - short no-grammar declarations used in real CLIA competition files
  - legacy inline grammar form
  - modern predeclared-nonterminal grammar form
- `src/sygus_solver.cpp` - added support for legacy bit-vector sort syntax `(BitVec n)`
- `tests/sygus_ut.cpp` - added parser coverage for:
  - short benchmark-style `synth-fun`
  - legacy `(BitVec n)` benchmark-style grammar declarations
- `CMakeLists.txt` - switched `add_test(...)` entries to `$<TARGET_FILE:...>` so local `ctest` resolves the built Windows executables correctly

Deletions / removals:
- none

Steps taken:
1. Inspected the public GitHub Actions summary page for run `ci #9` on commit `429ab34`.
2. Confirmed the visible failing jobs are:
  - `build-and-test`
  - `coverage`
3. Confirmed the Node 20 warnings shown on the run page are warnings only, not the primary failure cause.
4. Scanned the staged `third-party/sygus-benchmarks/comp/2019` tree and identified the dominant real syntax forms:
  - short `synth-fun` declarations without explicit grammars
  - old bit-vector sort syntax `(BitVec n)`
  - heavy use of invariant syntax via `synth-inv`, `declare-primed-var`, and `inv-constraint`
5. Patched the parser against those observed benchmark forms only.
6. Built a parser-only local executable and swept the entire staged `comp/2019` corpus.

Validation performed:
- public GitHub Actions summary page:
  - `https://github.com/munteanu-mihai-alin/SyGuS-Generator/actions/runs/25644300220`
  - observed:
    - `build-and-test` exit code `1`
    - `coverage` exit code `2`
- parser-only build:
  - `cmake -S . -B build-parse2019 -G Ninja -D SYGUS_BUILD_SOLVER=OFF -D SYGUS_BUILD_TESTS=OFF -D CMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe`
  - `cmake --build build-parse2019 -j 4`
- representative real-file parses:
  - `comp/2019/CLIA_Track/from_2018/arraysearch16.sl`
  - `comp/2019/General_Track/bv-conditional-inverses/find_inv_bvsge_bvadd_4bit.sl`
  - `comp/2019/Inv_Track/From2018/bk-nat.sl`
  - all returned success after the parser patch
- full staged 2019 sweep:
  - total checked: `2854`
  - total parse failures: `0`
- local no-cvc5 test build:
  - `cmake -S . -B build-local-nocvc5 -G Ninja -D SYGUS_BUILD_TESTS=ON -D SYGUS_BUILD_SOLVER=ON -D SYGUS_REQUIRE_CVC5=OFF -D CMAKE_DISABLE_FIND_PACKAGE_cvc5=TRUE -D CMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe`
  - `cmake --build build-local-nocvc5 -j 4`
  - `ctest --test-dir build-local-nocvc5 --output-on-failure`
  - result: all tests passed

Known risks / follow-up:
- The staged `comp/2019` corpus now parses successfully in parser-only mode, but that does not mean the solver can solve or even semantically handle those files yet.
- The precise root cause of the current remote `build-and-test` / `coverage` CI failures is still only partially inspected because:
  - `gh` is not installed in the current shell
  - the GitHub app token available in this session is expired
  - signed-out GitHub pages expose the run summary and annotations, but not full logs
- The next best move is to push the parser/test/CMake fixes and then inspect the next CI run, which should be narrower and easier to diagnose.

Suggested commit:
```bash
git commit -m "expand benchmark parser"
```

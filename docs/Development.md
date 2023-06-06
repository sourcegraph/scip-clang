# Working on scip-clang

- [Install dependencies](#install-dependencies)
- [Building](#building)
  - [Running the indexer](#running-the-indexer)
- [Running tests](#running-tests)
  - [Indexing large projects](#indexing-large-projects)
- [Formatting](#formatting)
- [IDE integration](#ide-integration)
- [Debugging](#debugging)
  - [Stacktraces](#stacktraces)
  - [Attaching a debugger](#attaching-a-debugger)
  - [Debugging on Linux](#debugging-on-linux)
  - [Inspecting Clang ASTs](#inspecting-clang-asts)
  - [Automated test case reduction](#automated-test-case-reduction)
  - [Debugging preprocessor issues](#debugging-preprocessor-issues)
  - [Debugging using a local Clang checkout](#debugging-using-a-local-clang-checkout)
- [Profiling](#profiling)
  - [Stack sampling](#stack-sampling)
  - [Tracing using Perfetto](#tracing-using-perfetto)
- [Publishing releases](#publishing-releases)
- [Implementation notes](#implementation-notes)
- [Notes on Clang internals](#notes-on-clang-internals)

## Install dependencies

1. [Bazelisk](https://github.com/bazelbuild/bazelisk): This handles Bazel versions
   transparently.
2. (Linux only) On Ubuntu, install `libc6-dev` for system headers like `feature.h`.

Bazel manages the C++ toolchain and other tool dependencies like formatters,
so they don't need to be downloaded separately.

## Building

(The `dev` config is for local development.)

```
# macOS
bazel build //... --spawn_strategy=local --config=dev

# Linux
bazel build //... --config=dev
```

The indexer binary will be placed at `bazel-bin/indexer/scip-clang`.

On macOS, `--spawn_strategy=local` provides a dramatic improvement
in incremental build times (~10x) and is highly recommended.
If you are more paranoid, instead use
`--experimental_reuse_sandbox_directories` which cuts down
on build times by 2x-3x, while maintaining sandboxing.

### Running the indexer

Example invocation for a CMake project:

```bash
# This will generate a compilation database under build/
# See https://clang.llvm.org/docs/JSONCompilationDatabase.html
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON <args>

# Invoked scip-clang from the project root (not the build root)
path/to/scip-clang --compdb-path build/compile_commands.json
```

Consult `--help` for user-facing flags, and `--help-all` for both user-facing and internal flags.

## Running tests

Run all tests:

```bash
bazel test //test --spawn_strategy=local --config=dev
```

Update snapshot tests:

```bash
bazel test //test:update --spawn_strategy=local --config=dev
```

NOTE: When adding a new test case, you need to manually create
an empty `.snapshot.cc` file for recording snapshot output
(it's not automatically generated).

Examples of running subsets of tests (follows directory structure):

```bash
bazel test //test:test_index --spawn_strategy=local --config=dev
bazel test //test:test_index_aliases --spawn_strategy=local --config=dev
bazel test //test:update_index --spawn_strategy=local --config=dev
bazel test //test:update_index_aliases --spawn_strategy=local --config=dev
```

### Indexing large projects

At the moment, we don't have any integration testing jobs
which index large projects in CI. Before making a release,
we typically manually test the indexer against one or more projects
([instructions](/docs/IndexingProjects.md)).

## Formatting

Run `./tools/reformat.sh` to reformat code and config files.

## IDE integration

Run `./tools/regenerate-compdb.sh` to generate a compilation database
at the root of the repository. It will be automatically
picked up by clangd-based editor extensions (you may
need to reload the editor).

## Debugging

### Stacktraces

The default modes of ASan and UBSan do not print stack traces on failures.
I recommend maintaining a parallel build of LLVM
at the same commit as in [fetch_deps.bzl](/fetch_deps.bzl).
Both sanitizers need access to `llvm-symbolizer` to print stack traces,
which can provided via the separate build.

```bash
# For ASan
ASAN_SYMBOLIZER_PATH="$PWD/../llvm-project/build/bin/llvm-symbolizer" ASAN_OPTIONS=symbolize=1 <scip-clang invocation>
# For UBSan
PATH="$PWD/../llvm-project/build/bin:$PATH" UBSAN_OPTIONS=print_stacktrace=1 <scip-clang invocation>
```

Anecdotally, on macOS, this can take 10s+ the first time around,
so don't hit Ctrl+C if UBSan seems to be stuck.

### Attaching a debugger

In the default mode of operation, the worker which runs semantic
analysis and emits the index, runs in a separate process and
performs IPC to communicate with the driver.
This makes using a debugger tedious.

If you want to attach a debugger, run the worker directly instead.

1. First, run the original `scip-clang` invocation with `--log-level=debug`
   and a short timeout (say `--receive-timeout-seconds=10`).
   This will print job ids (`<compdb-index>.<subtask-index>`)
   around when a task is being processed.
2. Subset out the original compilation database using `jq` or similar.
    ```bash
    jq '[.[<compdb-index>]]' compile_commands.json > bad.json
    ```
3. Run `scip-clang --worker-mode=compdb --compdb-path bad.json`
   (the original `scip-clang` invocation will have printed more arguments
   which were passed to the worker, but most of them
   should be unnecessary).

If you have not used LLDB before, check out this
[LLDB cheat sheet](https://www.nesono.com/sites/default/files/lldb%20cheat%20sheet.pdf).

### Debugging on Linux

There is a [VM setup script](/tools/vm-setup.sh) available
to configure a GCP VM for building scip-clang.
We recommend using Ubuntu 20.04+ with 16 cores or more.

### Inspecting Clang ASTs

Print the AST nodes:

```
clang -Xclang -ast-dump file.c
clang -Xclang -ast-dump=json file.c
```

Another option is to use clang-query ([tutorial](https://devblogs.microsoft.com/cppblog/exploring-clang-tooling-part-2-examining-the-clang-ast-with-clang-query/)).

### Automated test case reduction

In case of a crash, it may be possible to automatically reduce
it using [C-Reduce](https://github.com/csmith-project/creduce).

**Important:**
On macOS, use `brew install --HEAD creduce`,
as the default version is very outdated.

There is a helper script [tools/reduce.py](/tools/reduce.py)
which can coordinate `scip-clang` and `creduce`,
since correctly handling different kinds of paths in a compilation database
is a bit finicky in the general case.

It can be invoked like so:

```bash
# Pre-conditions:
# 1. CWD is project root
# 2. bad.json points to a compilation database with a single entry
#    known to cause the crash
/path/to/tools/reduce.py bad.json
```

After completion, a path to a reduced C++ file will be printed out
which still reproduces the crash.

See the script's `--help` text for information about additional flags.

### Debugging preprocessor issues

The LLVM monorepo contains a tool
[pp-trace](https://clang.llvm.org/extra/pp-trace.html)
which can be used to understand
the preprocessor callbacks being invoked
without having to resort to
print debugging inside scip-clang itself.

First, build `pp-trace` from source in your LLVM checkout,
making sure to include `clang-tools-extra` in `LLVM_ENABLE_PROJECTS`.
After that, it can be invoked like so:

```bash
# -isysroot is needed for pp-trace to find standard library headers
/path/to/llvm-project/build/bin/pp-trace mytestfile.cpp --extra-arg="-isysroot" --extra-arg="$(xcrun --show-sdk-path)" > pp-trace.yaml
```

See the [pp-trace](https://clang.llvm.org/extra/pp-trace.html) docs
or  the `--help` text for information about other supported flags.

One can check that the structure of the YAML file matches what we expect

```bash
bazel build //tools:analyze_pp_trace
./bazel-bin/tools/analyze_pp_trace --yaml-path pp-trace.yaml
```

### Debugging using a local Clang checkout

Sometimes, the best way to debug something is to be able to put print statements
inside Clang itself. For that, you can stub out the usage of `llvm-raw` in `fetch_deps.bzl`

```starlark
  # Comment out the corresponding http_archive call
  native.new_local_repository(
    name = "llvm-raw",
    path = "/home/me/code/llvm-project",
    build_file_content = "# empty",
  )
```

After that, add print debugging statements inside Clang (e.g. using `llvm::errs() <<`),
and rebuild `scip-clang` like usual.

## Profiling

### Stack sampling

One can create flamegraphs using
[Brendan Gregg's flamegraph docs](https://github.com/brendangregg/FlameGraph).

Two caveats on macOS:
- Invoking `dtrace` requires `sudo`.
- Once the stacks are folded, running ``sed -e 's/scip-clang`//g'``
  over the result should clean up the output a bit.

On macOS, if Xcode is installed, one can use `xctrace` for profiling.
Here's an example invocation:

```
xctrace record --template 'Time Profiler' --time-limit 60s --attach 'pid' --output out.trace
```

The resulting `out.trace` can be opened using Instruments.app.

### Tracing using Perfetto

First, build the Perfetto tools from source in a separate directory.

<!-- NOTE: Keep this version in sync with fetch_deps.bzl -->
```bash
git clone https://android.googlesource.com/platform/external/perfetto -b v33.1 && cd perfetto
tools/install-build-deps
tools/gn gen --args='is_debug=false' out/x
tools/ninja -C out/x tracebox traced traced_probes perfetto
```

Make sure that `scip-clang` is built in release mode
(using `--config=release`). In two different TTYs (e.g. tmux panes or iTerm tabs),
start `traced` and `perfetto` respectively:

```bash
# Terminal 1
out/x/traced

# Terminal 2
out/x/perfetto \
  --txt --config ~/Code/scip-clang/tools/long_trace.pbtx \
  --out "trace_$(date '+%Y-%m-%d_%H:%M:%S').pb"
```

Run the `scip-clang` invocation as usual in a separate terminal.

Once the `scip-clang` invocation ends,
kill the running `perfetto` process,
to flush any buffered data.

Open the saved trace file using the [online Perfetto UI](https://ui.perfetto.dev).

## Publishing releases

1. Manually double-check that
   [indexing works on one or more large projects](#indexing-large-projects).
2. Land a PR with the following:
   - A [CHANGELOG](/CHANGELOG.md) entry.
   - Version bump in [Version.h](/indexer/Version.h).
3. Once the PR is merged to main, run:
   ```bash
   NEW_VERSION="vM.N.P" bash -c 'git checkout main && git tag "$NEW_VERSION" && git push origin "$NEW_VERSION"'
   ```

The release workflow can also be triggered against any branch
in a "dry run" mode using the
[GitHub Actions UI](https://github.com/sourcegraph/scip-clang/actions/workflows/release.yml).

## Implementation notes

<!-- NOTE(def: based-on-sorbet) -->
Some useful non-indexer specific logic is adapted from the Sorbet
codebase and is marked with a `NOTE(ref: based-on-sorbet)`.

In particular, we reuse the infrastructure for `ENFORCE` macros,
which are essentially assertions which are instrumented so
that the cost can be measured easily.
We could technically have used `assert`,
but having a separate macro makes it easier to change
the behavior in scip-clang exclusively, whereas there is a
greater chance of mistakes if we want to separate out the
cost of assertions in Clang itself vs in our code.

## Notes on Clang internals

See [docs/SourceLocation.md](/docs/SourceLocation.md) for information
about how source locations are handled in Clang.

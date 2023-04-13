# Working on scip-clang

- [Install dependencies](#install-dependencies)
- [Building](#building)
  - [Running the indexer](#running-the-indexer)
- [Running tests](#running-tests)
- [Formatting](#formatting)
- [IDE integration](#ide-integration)
- [Debugging](#debugging)
  - [UBSan stacktraces](#ubsan-stacktraces)
  - [Attaching a debugger](#attaching-a-debugger)
  - [Debugging on Linux](#debugging-on-linux)
  - [Inspecting Clang ASTs](#inspecting-clang-asts)
  - [Automated test case reduction](#automated-test-case-reduction)
  - [Debugging preprocessor issues](#debugging-preprocessor-issues)
- [Implementation notes](#implementation-notes)
- [Notes on Clang internals](#notes-on-clang-internals)
- [Notes on Windows](#notes-on-windows)

## Install dependencies

1. [Bazelisk](https://github.com/bazelbuild/bazelisk): This handles Bazel versions
   transparently.

Bazel manages the C++ toolchain and other tool dependencies like formatters,
so they don't need to be downloaded separately.
(For unclear reasons, Bazel still requires
a host toolchain to be present for configuring _something_
but it will not be used for building the code in this project.)

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
bazel test //update --spawn_strategy=local --config=dev
```

## Formatting

Run `./tools/reformat.sh` to reformat code and config files.

## IDE integration

Run `./tools/regenerate-compdb.sh` to generate a compilation database
at the root of the repository. It will be automatically
picked up by clangd-based editor extensions (you may
need to reload the editor).

## Debugging

### UBSan stacktraces

The default mode of UBSan will not print stack traces on failures.
I recommend maintaining a parallel build of LLVM
at the same commit as in [fetch_deps.bzl](/fetch_deps.bzl).
UBSan needs a `llvm-symbolizer` binary on `PATH`
to print stack traces, which can provided via the separate build.

```bash
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

```
/path/to/llvm-project/build/bin/pp-trace mytestfile.cpp --extra-arg="-isysroot" --extra-arg="$(xcrun --show-sdk-path)"
```

The `isysroot` argument is particularly important,
as `pp-trace` will not find standard library headers without it.

See the [pp-trace](https://clang.llvm.org/extra/pp-trace.html) docs
or  the `--help` text for information about other supported flags.

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

## Notes on Windows

We have limited familiarity with Windows overall,
so this section includes detailed steps to (try to)
build the code on Windows.

1. Spin up a Windows Server 2022 machine on GCP.
   This generally takes a bit more time than Linux machines.
2. Install [Microsoft Remote Desktop](https://apps.apple.com/us/app/microsoft-remote-desktop/id1295203466)
   through the App Store.
3. Run the GCP command: (via RDP dropdown > View gcloud command to reset password)
   ```bash
   gcloud compute reset-windows-password --zone "<your zone>" --project <your project>" "<instane name>"
   ```
   This will print a password.
4. In the GCP UI, download the RDP file for remote login.
5. Open the RDP file using Microsoft Remote Desktop.
6. Enter the password from step 3.
7. Start Powershell.exe as Admin and [install Chocolatey](https://docs.chocolatey.org/en-us/choco/setup#install-with-powershell.exe)
8. Install [Git for Windows](https://github.com/git-for-windows/git/releases/).
9. Run Git Bash as Admin and install Python and Bazelisk:
   ```
   choco install -yv bazelisk python3
   ```
   After this, you may need to restart Git Bash for Python to be found.
   If after restarting, check if `python3 --version` and `python --version` work.
   If `python3 --version` doesn't work, then copy over the binary
   ```bash
   cp "$(which python)" "$(dirname "$(which python)")/python3"
   ```
10. Before invoking Bazel, make sure to run:
   ```bash
   export MSYS2_ARG_CONV_EXCL="*"
   ```
   for correctly handling `//` in Bazel targets.

After this, you should be able to run the build as usual.

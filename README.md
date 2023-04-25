# scip-clang: SCIP indexer for C and C++

Status: scip-clang currently supports single-repository precise code navigation
for C and C++. It is ready for early adopters, with codebases smaller than 15M SLOC.
Larger codebases may run into a [Protobuf limit on message sizes](https://stackoverflow.com/q/34128872/2682729)
([tracking issue for fix](https://github.com/sourcegraph/scip/issues/143)).

Code navigation examples in [llvm/llvm-project](https://sourcegraph.com/github.com/llvm/llvm-project):
- [Find references for #include](https://sourcegraph.com/github.com/llvm/llvm-project@97a03eb2eb5acf269db6253fe540626b52950f97/-/blob/llvm/include/llvm/ADT/SmallSet.h?L1:1-1:81#tab=references)
- [Find references for macros](https://sourcegraph.com/github.com/llvm/llvm-project@daad48d6b236d74c6b29daebba46289b98104241/-/blob/llvm/include/llvm/Support/Debug.h?L101:9-101:19#tab=references)
- [Find references for types](https://sourcegraph.com/github.com/llvm/llvm-project@daad48d6b236d74c6b29daebba46289b98104241/-/blob/clang/include/clang/AST/ASTContext.h?L1472:34-1472:45#tab=references)

<!-- Add Chromium index reference -->
<!-- Add Pytorch index -->
<!-- Add GIF of file navigation? -->

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [System Requirements](#system-requirements)
- [Usage](#usage)
  - [Generating a compilation database](#generating-a-compilation-database)
  - [Building code](#building-code)
  - [Initial scip-clang testing](#initial-scip-clang-testing)
  - [Running scip-clang on the entire codebase](#running-scip-clang-on-the-entire-codebase)
- [Troubleshooting](#troubleshooting)
- [Reporting issues](#reporting-issues)
- [Documentation](#documentation)
- [Contributing](#contributing)

## Supported Platforms

[Binary releases](https://github.com/sourcegraph/scip-clang/releases)
are available for x86_64 Linux (glibc 2.16 or newer) and x86_64 macOS
(supported on arm64 macOS via Rosetta).

We're exploring [Windows support](https://github.com/sourcegraph/scip-clang/issues/170).

Codebases using GCC and/or Clang for routine compilation
are both supported. For codebases exclusively built using GCC,
compatibility should be as good as Clang's compatibility
(i.e. most features should work, with graceful degradation
for features that don't).

scip-clang currently supports indexing using a
[JSON compilation database][].
CMake, Bazel and Meson support emitting this format
for compatibility with clang-based tooling.
We're interested in exploring more
[native Bazel support](https://github.com/sourcegraph/scip-clang/issues/182) in the future.

[JSON compilation database]: https://clang.llvm.org/docs/JSONCompilationDatabase.html

## Quick Start

The easiest way to use scip-clang, once you have a
[JSON compilation database][], is to invoke `scip-clang`
from the project root like so:

```bash
scip-clang --compdb-path=path/to/compile_commands.json
```

If you see any errors, see the
[Troubleshooting](#troubleshooting) section.

If all goes well, indexing will generate a file `index.scip`
which can be uploaded to a Sourcegraph instance using
[`src-cli`](https://github.com/sourcegraph/src-cli) v4.5 or newer.

```bash
# See https://docs.sourcegraph.com/cli/references/code-intel/upload
# Make sure to authenticate earlier or provide an access token
src code-intel upload -file=index.scip
```

See the [Usage](#usage) section for step-by-step instructions.

## System Requirements

1. About 2MB of temporary space per compilation database entry.
   ```bash
   echo "$(perl -e "print $(jq 'length' build/compile_commands.json) / 512.0") GB"
   ```
2. On Linux, about 2MB of space in `/dev/shm` per core (`df -h /dev/shm`).
   This may particularly be an issue when using Docker on a high core
   count machine, as default size of `/dev/shm` in Docker is 64MB.
   See also: how to [troubleshoot low disk space for IPC](/docs/Troubleshooting.md#disk-space-for-ipc).
3. 2GB RAM per core is generally sufficient.

## Usage

### Generating a compilation database

- **CMake**: Add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  to the `cmake` invocation. For typical projects, the overall
  invocation will look like:

  ```bash
  cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  ```

- **Bazel**: Use either
  [hedronvision/bazel-compile-commands-extractor](bazel-compile-commands-extractor)
  or [grailbio/bazel-compilation-database](https://github.com/grailbio/bazel-compilation-database).

- **Meson**: Use the Ninja backend,
  [which generates a compilation database](https://sourcegraph.com/search?q=context:global+repo:%5Egithub%5C.com/mesonbuild/meson%24+compile_commands.json&patternType=standard&sm=1&groupBy=path).

### Building code

Large projects typically use various forms of code generation.
scip-clang re-runs type-checking, so it needs access
to generated code. This means that `scip-clang` should preferably
run after building compilation artifacts.

### Initial scip-clang testing

For large codebases, we recommend first testing scip-clang
on a subset of a compilation database with diagnostics turned on.
For example:

```bash
# Using jq (https://github.com/stedolan/jq)
jq '.[0:5]' build/compile_commands.json > build/small_compdb.json
scip-clang --compdb-path=build/small_compdb.json --show-compiler-diagnostics
```

If there are errors about missing system or SDK headers,
install the relevant system dependencies.

If there are errors about missing generated headers,
make sure to [build your code first](#building-code).

If there are any other errors,
such as standard library or platform headers not being found,
please [report an issue](#reporting-issues).

### Running scip-clang on the entire codebase

```bash
scip-clang --compdb-path=build/compile_commands.json
```

The `--show-compiler-diagnostics` flag is deliberately omitted here,
since scip-clang is still able to index code in the presence of
compiler errors, and any errors in headers will get repeated
for each translation unit in which the header is included.

## Troubleshooting

See the [Troubleshooting docs](/docs/Troubleshooting.md).

## Reporting issues

Create a new [GitHub issue](https://github.com/sourcegraph/scip-clang/issues/new)
with any relevant logs attached.

Sourcegraph customers may ask their Customer Engineers
for help with filing an issue confidentally, as the log may
contain information about file names etc. 

## Documentation

Run `scip-clang --help` to see documentation for different flags.

A [CHANGELOG](CHANGELOG.md) is also available.

## Contributing

- [Development.md](/docs/Development.md) covers build instructions etc.
- [Design.md](/docs/Design.md) covers the high-level architecture and design considerations.
- [This GitHub comment](https://github.com/sourcegraph/sourcegraph/issues/42280#issuecomment-1352587026)
  covers why we decided to write a new C++ indexer.

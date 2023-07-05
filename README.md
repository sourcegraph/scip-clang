# scip-clang: SCIP indexer for C and C++ ![(Status: Beta)](https://img.shields.io/badge/status-beta-yellow?style=flat)

scip-clang is a precise code indexer based on Clang 16,
which supports cross-repository code navigation for C and C++
in Sourcegraph.

Here are some code navigation examples:
- [Cross-repository navigation in Boost](https://sourcegraph.com/github.com/boostorg/assert@f10ddd608e087a89ee5bfc41cf2987cc5ef61473/-/blob/include/boost/assert.hpp?L60:10-60:22#tab=references)
- In [Chromium](https://sourcegraph.com/github.com/chromium/chromium@b21c706/-/blob/base/atomic_ref_count.h?L19:7-19:21#tab=references).
- In [llvm/llvm-project](https://sourcegraph.com/github.com/llvm/llvm-project):
  - [Find references for #include](https://sourcegraph.com/github.com/llvm/llvm-project@97a03eb2eb5acf269db6253fe540626b52950f97/-/blob/llvm/include/llvm/ADT/SmallSet.h?L1:1-1:81#tab=references)
  - [Find references for macros](https://sourcegraph.com/github.com/llvm/llvm-project@daad48d6b236d74c6b29daebba46289b98104241/-/blob/llvm/include/llvm/Support/Debug.h?L101:9-101:19#tab=references)
  - [Find references for types](https://sourcegraph.com/github.com/llvm/llvm-project@daad48d6b236d74c6b29daebba46289b98104241/-/blob/clang/include/clang/AST/ASTContext.h?L1472:34-1472:45#tab=references)

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/sourcegraph/scip-clang/assets/93103176/08b3aa95-c3ee-4c56-9920-20dfa4a7070d">
  <img alt="Boost cross-repository Find References screenshot" src="https://github.com/sourcegraph/scip-clang/assets/93103176/1baf9a40-37ac-4896-bd1b-dc453730f91b">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/sourcegraph/scip-clang/assets/93103176/54cc557e-16c8-4890-b9d1-b40d6e215084">
  <img alt="Chromium code navigation screenshot" src="https://github.com/sourcegraph/scip-clang/assets/93103176/55ec1429-eef0-4b7d-a99a-0c3c6af23e92">
</picture>

## Table of Contents

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [System Requirements](#system-requirements)
- [Usage](#usage)
  - [Generating a compilation database](#generating-a-compilation-database)
  - [Building code](#building-code)
  - [Initial scip-clang testing](#initial-scip-clang-testing)
  - [Running scip-clang on a single repo](#running-scip-clang-on-the-entire-codebase)
  - [Setting up cross-repo code navigation](#setting-up-cross-repo-code-navigation)
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
Projects which use Make or other build systems may
be able to use [Bear](https://github.com/rizsotto/Bear)
to intercept compilation commands and generate a compilation database.

We're interested in exploring more
[native Bazel support](https://github.com/sourcegraph/scip-clang/issues/182) in the future.

The use of [pre-compiled headers](https://en.wikipedia.org/wiki/Precompiled_header)
is not supported, as the format of pre-compiled headers varies
across compilers and individual compiler versions.

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

1. About 2MB of temporary space for every TU in the compilation database.
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
  Caveat: The grailbio generator sometimes accidentally adds
  unexpanded Make variables in compilation commands,
  so you may need to remove them as a preprocessing step,
  before invoking `scip-clang`.

- **Meson**: Use the Ninja backend,
  [which generates a compilation database](https://sourcegraph.com/search?q=context:global+repo:%5Egithub%5C.com/mesonbuild/meson%24+compile_commands.json&patternType=standard&sm=1&groupBy=path).

- **Make** or other build systems: Use [Bear](https://github.com/rizsotto/Bear)
  to wrap the build system invocation which can build all the code. For example:

  ```bash
  bear -- make all
  ```

  In our testing on Linux, Bear works with Boost's B2 build system as well.

  Some other tools which may work include:
  - [compiledb](https://github.com/nickdiego/compiledb) (Linux, macOS, Windows):
    For Make-style systems, supposedly faster than Bear as it doesn't require a clean build.
  - [compile-db-gen](https://github.com/sunlin7/compile-db-gen) (Linux): Uses strace.
  - [clade](https://github.com/17451k/clade) (Linux, macOS, partial Windows support).

  We have not tested any of these.

The [official Clang docs](https://clang.llvm.org/docs/JSONCompilationDatabase.html#supported-systems)
may also have additional suggestions for generating a compilation database.

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

### Running scip-clang on a single repo

```bash
scip-clang --compdb-path=build/compile_commands.json
```

The `--show-compiler-diagnostics` flag is deliberately omitted here,
since scip-clang is still able to index code in the presence of
compiler errors, and any errors in headers will get repeated
for each translation unit in which the header is included.

### Setting up cross-repo code navigation

See the [cross-repository setup docs](/docs/CrossRepo.md).

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

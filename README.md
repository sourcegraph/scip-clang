# scip-clang: SCIP indexer for C and C++

Status: Ready for early adopters.

<!-- Add BETA status badge -->
<!-- Add Chromium index reference -->
<!-- Add Pytorch index -->
<!-- Add GIF of file navigation? -->

- [Supported Platforms](#supported-platforms)
- [Quick Start](#quick-start)
- [Usage](#usage)
  - [Generating a compilation database](#generating-a-compilation-database)
  - [Building code](#building-code)
  - [Initial scip-clang testing](#initial-scip-clang-testing)
  - [Running scip-clang on the entire codebase](#running-scip-clang-on-the-entire-codebase)
- [Troubleshooting](#troubleshooting)
  - [Timeouts](#timeouts)
  - [Crashes](#crashes)
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

The next section covers more general usage.

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

scip-clang may report errors and/or warnings
for diagnosing issues even when indexing completes successfully,
which is marked by a `Finished indexing ...` message,
along with a non-empty `index.scip` file.

The most common problems are timeouts and crashes,
which are discussed below.

### Timeouts

scip-clang sets a timeout for indexing an individual translation unit,
so that any hangs or crashes during indexing a single translation unit
do not cause indexing to fail.

The default timeout value is 5 minutes,
which should be sufficient to handle most code.
That said, if you see this error a lot,
[file a bug report](#reporting-issues).

If you're debugging an issue using a `-dev` build of `scip-clang`,
this value may need to be adjusted,
as the dev build performs more runtime checks.
To change the tieout value, pass `--receive-timeout-seconds 600`
when invoking `scip-clang`.
However, we do not recommend using this flag with the release build,
as a higher value will lead to [reduced concurrency](https://github.com/sourcegraph/scip-clang/issues/45)
for the duration of the timeout value if an indexing process crashes.

### Crashes

If you are able, run the following with a [`-dev` binary](https://github.com/sourcegraph/scip-clang/releases)
for your release.

Re-run the failing `scip-clang` command with `--log-level=debug`
and `--show-compiler-diagnostics`, and include those
when you [submit an issue](#reporting-issues)

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

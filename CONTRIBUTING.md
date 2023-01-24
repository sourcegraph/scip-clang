# Working on scip-clang

## Install dependencies

1. C++ toolchain: Used for bootstrapping. (Clang recommended; GCC
   works too, but emits some useless warnings when building dependencies.)
2. [Bazelisk](https://github.com/bazelbuild/bazelisk): This handles Bazel versions
   transparently.

## Building

(The `dev` config is for local development.)

```
# macOS
bazel build //... --spawn_strategy=local --config=dev

# Linux
export CC=clang
export CXX=clang++
bazel build //... --config=dev
```

The indexer binary will be located under `bazel-bin/indexer/scip-clang`.

On macOS, `--spawn_strategy=local` provides a dramatic improvement
in incremental build times (~10x) and is highly recommended.
If you are more paranoid, instead use
`--experimental_reuse_sandbox_directories` which cuts down
on build times by 2x-3x, while maintaining sandboxing.


## Running tests

Run all tests:

```bash
bazel test //test --spawn_strategy=local --config=dev
```

Update snapshot tests:

```bash
bazel test //update --spawn_strategy=local --config=dev
```

## Reformat code and config files

Run `./tools/reformat.sh`.

## IDE Integration

Run `./tools/regenerate-compdb.sh` to generate a compilation database
at the root of the repository. It will be automatically
picked up by clangd-based editor extensions (you may
need to reload the editor).

## Debugging

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

## Implementation Notes

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

- A `FileID`, unlike the name suggests, can refer to a File or
  a macro expansion.
- A valid `FileID` always has a corresponding `SLocEntry`.
- A `FileEntry` is only present for a File-representing `FileID`
  if it corresponds to an actual file. It will be missing
  if the `FileID` corresponds to an imaginary file
  (e.g. builtins). Thus, `sourceManager.getFileEntryForID` can
  return null for certain valid FileIDs.

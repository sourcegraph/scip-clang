# Working on scip-clang

## Install dependencies

1. C++ toolchain (gcc or clang): Used for bootstrapping.
2. [Bazelisk](https://github.com/bazelbuild/bazelisk): This handles Bazel versions
   transparently.

## Building

```
bazel build //... --sandbox_strategy=local
```

On macOS, `--sandbox_strategy=local` provides a dramatic improvement
in incremental build times (~10x) and is highly recommended.
If you are more paranoid, instead use
`--experimental_reuse_sandbox_directories` which cuts down
on build times by 2x-3x, while maintaining sandboxing.

Add `--config=dbg` if you want a debug build for stepping through
with a debugger.

## Running tests

Run all tests:

```
bazel test //test --sandbox_strategy=local
```

Update snapshot tests:

```
bazel test //update --sandbox_strategy=local
```

## Reformat code and config files

Run `./tools/reformat.sh`.

## IDE Integration

Run `./tools/regen_compdb.sh` to generate a compilation database
at the root of the repository. It will be automatically
picked up by clangd-based editor extensions (you may
need to reload the editor).
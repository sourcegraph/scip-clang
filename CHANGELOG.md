# scip-clang ChangeLog

## v0.0.5 (testing)

- Fixed spurious error messages showing up during shutdown,
  where worker and driver both wait for each other.
  (https://github.com/sourcegraph/scip-clang/pull/214)
- We now check that the `"file"` keys in a compilation database
  reference files that actually exist on disk, before starting indexing.
  This prevents an assertion from being triggered due to indexing failure.
  (https://github.com/sourcegraph/scip-clang/pull/209)

## v0.0.4 (testing)

- Improved handling and docs for IPC-related errors, such as when
  the amount of space available is limited (common in Docker),
  as well as when a message size exceeds the IPC buffer size.
  (https://github.com/sourcegraph/scip-clang/pull/187)
- Improved error messages when certain assertions are hit in practice.
  (https://github.com/sourcegraph/scip-clang/pull/196,
  https://github.com/sourcegraph/scip-clang/pull/200,
  https://github.com/sourcegraph/scip-clang/pull/205)
- LLVM assertions are turned off in release builds.
  scip-ruby's own `ENFORCE` checks are still enabled for debuggability.
  (https://github.com/sourcegraph/scip-clang/pull/203)

## v0.0.3 (testing)

- Added documentation with adoption tips, including how to
  troubleshoot common errors.
  (https://github.com/sourcegraph/scip-clang/pull/186)
- JobIDs printed when running `scip-clang` with `--log-level=debug`
  are now more informative.
  (https://github.com/sourcegraph/scip-clang/pull/189)
- (Dev builds) Added workaround for ENFORCE being hit on encountering
  previously unseen files during index merging.
  (https://github.com/sourcegraph/scip-clang/pull/194)
- (Dev builds) Fixed an ENFORCE being hit around pre-processor handling.
  (https://github.com/sourcegraph/scip-clang/issues/156)

## v0.0.2 (testing)

- Adds support for automatically inferring correct include
  directories for gcc/g++. This means that the indexer will
  correctly find standard library headers even when a
  `compile_commands.json` file refers to gcc/g++ instead of clang.
  (https://github.com/sourcegraph/scip-clang/pull/178)

## v0.0.1 (testing)

- Symbols without hover docs will explicitly show "No documentation available".
  (https://github.com/sourcegraph/scip-clang/pull/173)
- Published binaries should work on Debian Buster and Ubuntu 18.04,
  instead of requiring Debian Bullseye / Ubuntu 20.04 or newer.
  (https://github.com/sourcegraph/scip-clang/pull/174)

## v0.0.0 (testing)

- Initial release with code nav support for various
  language features like macros, #include pragmas, types,
  functions, methods, local variables etc.

# scip-clang ChangeLog

## v0.0.7 (alpha)

- Fixed an issue where using-declarations (`using X::Y::Z;`) were
  missing definition and reference information.
  (https://github.com/sourcegraph/scip-clang/pull/240)
- Fixed an issue where references to typedefs and alias declarations
  (`using A = B`) were not emitted correctly.
  (https://github.com/sourcegraph/scip-clang/pull/237)

## v0.0.6 (alpha)

- Added bulk processing for worker results.
  (https://github.com/sourcegraph/scip-clang/pull/222)
- Fixed an incorrect assertion about worker<->job mappings.
  This should prevent a driver crash when many workers run into a timeout.
  (https://github.com/sourcegraph/scip-clang/pull/222)
- The indexing summary printed at the end now includes the number
  of TUs which failed to be indexed.
  (https://github.com/sourcegraph/scip-clang/pull/225)
- Some basic performance optimizations for the core driver loop,
  such as reusing buffers
  (https://github.com/sourcegraph/scip-clang/pull/219)
  and removing wait operations for worker exits out of the core loop.
  (https://github.com/sourcegraph/scip-clang/pull/218)

## v0.0.5 (alpha)

- Fixed spurious error messages showing up during shutdown,
  where worker and driver both wait for each other.
  (https://github.com/sourcegraph/scip-clang/pull/214)
- We now check that the `"file"` keys in a compilation database
  reference files that actually exist on disk, before starting indexing.
  This prevents an assertion from being triggered due to indexing failure.
  (https://github.com/sourcegraph/scip-clang/pull/209)

## v0.0.4 (alpha)

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

## v0.0.3 (alpha)

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

## v0.0.2 (alpha)

- Adds support for automatically inferring correct include
  directories for gcc/g++. This means that the indexer will
  correctly find standard library headers even when a
  `compile_commands.json` file refers to gcc/g++ instead of clang.
  (https://github.com/sourcegraph/scip-clang/pull/178)

## v0.0.1 (alpha)

- Symbols without hover docs will explicitly show "No documentation available".
  (https://github.com/sourcegraph/scip-clang/pull/173)
- Published binaries should work on Debian Buster and Ubuntu 18.04,
  instead of requiring Debian Bullseye / Ubuntu 20.04 or newer.
  (https://github.com/sourcegraph/scip-clang/pull/174)

## v0.0.0 (alpha)

- Initial release with code nav support for various
  language features like macros, #include pragmas, types,
  functions, methods, local variables etc.

#ifndef SCIP_CLANG_VERSION_H
#define SCIP_CLANG_VERSION_H

#if !defined(NDEBUG) || defined(FORCE_DEBUG)
#define DEBUG_MODE
#else
#undef DEBUG_MODE
#endif

namespace scip_clang {

#ifdef DEBUG_MODE
constexpr bool debugMode = true;
#else
constexpr bool debugMode = false;
#endif

#define VERSION "0.0.2"
#define LLVM_COMMIT \
  "b6e344ce91c8796331fca7644eb8c748ac5391ec" // Keep in sync with setup.bzl

// TODO: Add scip-clang Git SHA + dirty bit.

const char version[] = VERSION;

const char full_version_string[] =
    ("scip-clang " VERSION "\nBased on Clang/LLVM " LLVM_COMMIT "\n");

#undef VERSION
#undef LLVM_COMMIT

} // namespace scip_clang

#endif // SCIP_CLANG_VERSION_H
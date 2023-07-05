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

#define VERSION "0.2.3"
#define LLVM_COMMIT \
  "e0f3110b854a476c16cce7b44472cd7838d344e9" // Keep synced with fetch_deps.bzl

// TODO: Add scip-clang Git SHA + dirty bit.

const char version[] = VERSION;

const char full_version_string[] =
    ("scip-clang " VERSION "\nBased on Clang/LLVM " LLVM_COMMIT "\n");

#undef VERSION
#undef LLVM_COMMIT

} // namespace scip_clang

#endif // SCIP_CLANG_VERSION_H

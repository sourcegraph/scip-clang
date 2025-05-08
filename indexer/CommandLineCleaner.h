#ifndef SCIP_CLANG_COMMAND_LINE_CLEANER_H
#define SCIP_CLANG_COMMAND_LINE_CLEANER_H

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "llvm/Support/Regex.h"

namespace scip_clang::compdb {

enum class CliOptionKind {
  NoArgument,
  OneArgument,
};

struct CommandLineCleaner {
  using MapType = absl::flat_hash_map<std::string_view, CliOptionKind>;
  // Fixed list of options for which the command-line arguments should be
  // zapped. If CliOptionKind is NoArgument, then only one string will be
  // zapped. If CliOptionKind is OneArgument, then two successive strings will
  // be zapped.
  MapType toZap;
  // Optional matcher for zapping arguments more flexibly.
  // This is to allow for handling unknown flags which match a particular
  // pattern. For known flags, put them in toZap.
  std::optional<llvm::Regex> noArgumentMatcher;

  void clean(std::vector<std::string> &commandLine) const;

  static std::unique_ptr<CommandLineCleaner> forClangOrGcc();
};

} // namespace scip_clang::compdb

#endif
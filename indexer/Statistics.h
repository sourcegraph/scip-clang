#ifndef SCIP_CLANG_STATISTICS_H
#define SCIP_CLANG_STATISTICS_H

#include "llvm/Support/JSON.h"

#include "indexer/IpcMessages.h"

namespace scip_clang {

struct StatsEntry {
  std::string path;
  IndexingStatistics stats;

  static void emitAll(std::vector<StatsEntry> &&stats, std::string_view path);
};

llvm::json::Value toJSON(const StatsEntry &entry);

} // namespace scip_clang

#endif
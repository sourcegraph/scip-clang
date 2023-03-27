#include <string_view>
#include <vector>

#include "llvm/Support/JSON.h"

#include "indexer/Statistics.h"

namespace scip_clang {

llvm::json::Value toJSON(const StatsEntry &entry) {
  auto &stats = entry.stats;
  return llvm::json::Object{
      {"filepath", entry.path},
      {"stats",
       llvm::json::Object{
           {"total_time_s", double(stats.totalTimeMicros) / 1'000'000.0},
       }}};
}

// static
void StatsEntry::emitAll(std::vector<StatsEntry> &&stats,
                         std::string_view path) {
  std::error_code error;
  llvm::raw_fd_ostream out(path, error);
  ENFORCE(!error);
  llvm::json::OStream jsonStream(out);
  jsonStream.array([&]() {
    for (auto &statEntry : stats) {
      jsonStream.value(llvm::json::Value(statEntry));
    }
  });
  jsonStream.flush();
}

} // namespace scip_clang
#ifndef SCIP_CLANG_PROGRESS_REPORTER_H
#define SCIP_CLANG_PROGRESS_REPORTER_H

#include <cstdint>
#include <string_view>

namespace scip_clang {

class ProgressReporter {
  std::string_view message;
  size_t totalCount;
  size_t countWidth;
  bool active;
  bool isTty;

public:
  ProgressReporter(bool active, std::string_view msg, size_t totalCount);

  void report(size_t count, std::string_view extraData) const;

  ~ProgressReporter();
};

} // namespace scip_clang

#endif // SCIP_CLANG_PROGRESS_REPORTER_H
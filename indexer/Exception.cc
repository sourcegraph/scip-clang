#include <execinfo.h>
#include <string>
#include <string_view>

#include "indexer/Exception.h"
#include "indexer/os/Os.h"

#include "spdlog/spdlog.h"

#define MAX_STACK_FRAMES 128
static void *stackTraces[MAX_STACK_FRAMES];

static void filter_unnecessary(std::string &out) {
  size_t i = 0;
  size_t j = 0;
  using namespace std::string_view_literals;
  std::string_view patterns[] = {"typecase.h:"sv, "__functional_base:"sv,
                                 "functional:"sv};

  while (i < out.length()) {
    i = out.find('\n', i);
    if (i == std::string::npos) {
      break;
    }
    j = out.find('\n', i + 1);
    if (j == std::string::npos) {
      break;
    }
    bool found = false;
    std::string substr(out, i, j - i);
    for (auto &subp : patterns) {
      found = found || (substr.find(subp) != std::string::npos);
    }
    if (found) {
      out.erase(i, j - i);
    } else {
      i = i + 1;
    }
  }
}

namespace scip_clang {

std::string exceptionContext = "";

void Exception::printBacktrace() noexcept {
  int traceSize = 0;
  auto **messages = (char **)nullptr;
  std::string programName = getProgramName();

  traceSize = backtrace(stackTraces, MAX_STACK_FRAMES);
  messages = backtrace_symbols(stackTraces, traceSize);

  std::string res = scip_clang::addr2line(programName, stackTraces, traceSize);
  ::filter_unnecessary(res);
  spdlog::error("Backtrace:\n{}", res.c_str());

  if (messages != nullptr) {
    free(messages);
  }
}

} // namespace scip_clang

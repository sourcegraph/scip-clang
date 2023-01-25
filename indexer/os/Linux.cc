#ifdef __linux__
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "absl/debugging/symbolize.h"
#include "spdlog/spdlog.h"

#include "indexer/os/Os.h"

extern "C" {
// If we are linked against the LLVM sanitizers, this symbol will be
// replaced with the definition from the sanitizer runtime
void __attribute__((weak))
__sanitizer_symbolize_pc(void *pc, const char *fmt, char *outBuf,
                         size_t outBufSize) {
  (void)pc;
  (void)fmt;
  std::snprintf(outBuf, outBufSize, "<null>");
}
}

static void symbolize_pc(void *pc, const char *fmt, char *outBuf,
                         size_t outBufSize) {
  __sanitizer_symbolize_pc(pc, fmt, outBuf, outBufSize);
  outBuf[outBufSize - 1] = '\0';
  if (strstr(outBuf, "<null>")
      != nullptr) { // sanitizers were'nt able to symbolize it.
    auto offset = snprintf(outBuf, outBufSize, "%p ", pc);
    absl::Symbolize(pc, outBuf + offset, outBufSize - offset);
  }
}

namespace scip_clang {

std::string addr2line(std::string_view programName, void const *const *addr,
                      int count) {
  (void)programName;
  fmt::memory_buffer os;
  for (int i = 3; i < count; ++i) {
    char buf[4096];
    symbolize_pc(const_cast<void *>(addr[i]), "%p in %f %s:%l:%c", buf,
                 sizeof(buf));
    fmt::format_to(std::back_inserter(os), "  #{} {}\n", i, buf);
  }
  return to_string(os);
}
std::string getProgramName() {
  char dest[512] = {}; // explicitly zero out

  if (readlink("/proc/self/exe", dest, PATH_MAX) < 0) {
    return "<error>";
  }

  std::string res(dest);
  return res;
}

bool amIBeingDebugged() {
  // TracerPid was added into linux in ~2005. Should work on all linuxes since
  // then
  char buf[4096];
  // cargo culted from
  // https://stackoverflow.com/questions/3596781/how-to-detect-if-the-current-process-is-being-run-by-gdb

  const int statusFd = ::open("/proc/self/status", O_RDONLY);
  if (statusFd == -1) {
    return false;
  }

  const ssize_t numRead = ::read(statusFd, buf, sizeof(buf) - 1);
  ::close(statusFd);
  if (numRead <= 0) {
    return false;
  }

  buf[numRead] = '\0';
  constexpr char tracerPidString[] = "TracerPid:";
  const auto tracerPidPtr = ::strstr(buf, tracerPidString);
  if (!tracerPidPtr) {
    return false; // Can't tell
  }

  for (const char *characterPtr = tracerPidPtr + sizeof(tracerPidString) - 1;
       characterPtr <= buf + numRead; ++characterPtr) {
    if (::isspace(*characterPtr)) {
      continue;
    }
    return ::isdigit(*characterPtr) != 0 && *characterPtr != '0';
  }

  return false;
}

bool setCurrentThreadName(std::string_view name) {
  const size_t maxLen =
      16 - 1; // Pthreads limits it to 16 bytes including trailing '\0'
  auto truncatedName = std::string(name.substr(0, maxLen));
  auto retCode = ::pthread_setname_np(::pthread_self(), truncatedName.c_str());
  return retCode == 0;
}

} // namespace scip_clang

#endif

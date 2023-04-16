#ifdef __APPLE__
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#import <mach/thread_act.h>
#include <string>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <variant>

#include "spdlog/spdlog.h"

#include "indexer/os/Os.h"

namespace scip_clang {

std::string addr2line(std::string_view programName, void const *const *addr,
                      int count) {
  auto addr2lineCmd =
      fmt::format("atos -o {} -p {}", programName, (int)getpid());
  for (int i = 3; i < count; ++i) {
    addr2lineCmd = fmt::format("{} {}", addr2lineCmd, addr[i]);
  }

  return scip_clang::exec(addr2lineCmd);
}
std::string getProgramName() {
  char buf[512];
  uint32_t sz = 512;
  _NSGetExecutablePath(buf, &sz);
  std::string res(buf);
  return res;
}

/** taken from https://developer.apple.com/library/content/qa/qa1361/_index.html
 */
// Returns true if the current process is being debugged (either
// running under the debugger or has a debugger attached post facto).
bool amIBeingDebugged() {
  int junk __attribute__((unused));
  int mib[4];
  struct kinfo_proc info;
  size_t size;

  // Initialize the flags so that, if sysctl fails for some bizarre
  // reason, we get a predictable result.

  info.kp_proc.p_flag = 0;

  // Initialize mib, which tells sysctl the info we want, in this case
  // we're looking for information about a specific process ID.

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();

  // Call sysctl.

  size = sizeof(info);
  junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, nullptr, 0);
  assert(junk == 0);

  // We're being debugged if the P_TRACED flag is set.

  return ((info.kp_proc.p_flag & P_TRACED) != 0);
}

bool setCurrentThreadName(std::string_view name) {
  const size_t maxLen =
      16 - 1; // Pthreads limits it to 16 bytes including trailing '\0'
  auto truncatedName = std::string(name.substr(0, maxLen));
  auto retCode = ::pthread_setname_np(truncatedName.c_str());
  return retCode == 0;
}

std::variant<uint64_t, std::error_code> availableSpaceForIpc() {
  // TODO: Figure out a good way to calculate this on macOS.
  return scip_clang::availableSpaceUnknown;
}

} // namespace scip_clang

#endif
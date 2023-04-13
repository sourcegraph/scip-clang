#ifdef _WIN32

#include <string>

#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <windef.h>
#include <windows.h>

#include "indexer/os/Os.h"

namespace scip_clang {

std::string exec(std::string cmd) {
  // FIXME(def: windows-support) Implement this if needed for addr2line
  return "";
}

std::string addr2line(std::string_view programName, void const *const *addr,
                      int count) {
  // FIXME(def: windows-support)
  return "";
}

std::string getProgramName() {
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return buf;
}

bool setCurrentThreadName(std::string_view name) {
  std::wstring wstr = std::wstring(name.begin(), name.end());
  SetThreadDescription(GetCurrentThread(), wstr.c_str());
  return true;
}

bool amIBeingDebugged() {
  // FIXME(def: windows-support)
  return false;
}

bool stopInDebugger() {
  // FIXME(def: windows-support)
  return false;
}

} // namespace scip_clang

#endif
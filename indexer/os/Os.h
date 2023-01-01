#ifndef SCIP_CLANG_OS_H
#define SCIP_CLANG_OS_H

// NOTE(ref: based-on-sorbet): Heavily stripped down from Sorbet's
// common/os/os.h

#include <string>
#include <string_view>

namespace scip_clang {

std::string exec(std::string cmd);

std::string addr2line(std::string_view programName, void const *const *addr,
                      int count);

std::string getProgramName();

bool setCurrentThreadName(std::string_view name);

bool amIBeingDebugged();

/** The should trigger debugger breakpoint if the debugger is attached, if no
 * debugger is attach, it should do nothing This allows to:
 *   - have "persistent" break points in development loop, that survive line
 * changes.
 *   - test the same executable outside of debugger without rebuilding.
 * */
bool stopInDebugger();

} // namespace scip_clang

#endif // SCIP_CLANG_OS_H
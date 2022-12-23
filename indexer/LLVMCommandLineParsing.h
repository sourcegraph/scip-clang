#ifndef SCIP_CLANG_LLVM_COMMAND_LINE_PARSING_H
#define SCIP_CLANG_LLVM_COMMAND_LINE_PARSING_H

#include <string>
#include <vector>

#include "clang/Tooling/JSONCompilationDatabase.h"
#include "llvm/ADT/StringRef.h"

namespace scip_clang {

std::vector<std::string>
unescapeCommandLine(clang::tooling::JSONCommandLineSyntax Syntax,
                    llvm::StringRef EscapedCommandLine);

} // namespace scip_clang

#endif // SCIP_CLANG_LLVM_COMMAND_LINE_PARSING_H
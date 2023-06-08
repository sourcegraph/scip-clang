#include <string>
#include <vector>

#include "clang/Tooling/JSONCompilationDatabase.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/StringSaver.h"

#include "indexer/Enforce.h"
#include "indexer/LlvmCommandLineParsing.h"

// --------------------------- ATTENTION -------------------------------------
// The code in this file is vendored from Clang's JSONCompilationDatabase.cpp
// because the parser is not exposed in any header.
//
// We could potentially have instead reused the APIs TokenizeWindowsCommandLine
// and TokenizeGNUCommandLine from llvm/Support/CommandLine.h, but it is unclear
// if TokenizeGNUCommandLine is 100% equivalent to CommandLineArgumentParser
// below.
//
// So I've copied the code so that we're 100% compatible with Clang's own
// parsing.

using JSONCommandLineSyntax = clang::tooling::JSONCommandLineSyntax;
using StringRef = llvm::StringRef;

namespace scip_clang {
namespace {

/// A parser for escaped strings of command line arguments.
///
/// Assumes \-escaping for quoted arguments (see the documentation of
/// unescapeCommandLine(...)).
class CommandLineArgumentParser {
public:
  CommandLineArgumentParser(StringRef CommandLine)
      : Input(CommandLine), Position(Input.begin() - 1) {}

  std::vector<std::string> parse() {
    bool HasMoreInput = true;
    while (HasMoreInput && nextNonWhitespace()) {
      std::string Argument;
      HasMoreInput = parseStringInto(Argument);
      CommandLine.push_back(Argument);
    }
    return CommandLine;
  }

private:
  // All private methods return true if there is more input available.

  bool parseStringInto(std::string &String) {
    do {
      if (*Position == '"') {
        if (!parseDoubleQuotedStringInto(String))
          return false;
      } else if (*Position == '\'') {
        if (!parseSingleQuotedStringInto(String))
          return false;
      } else {
        if (!parseFreeStringInto(String))
          return false;
      }
    } while (*Position != ' ');
    return true;
  }

  bool parseDoubleQuotedStringInto(std::string &String) {
    if (!next())
      return false;
    while (*Position != '"') {
      if (!skipEscapeCharacter())
        return false;
      String.push_back(*Position);
      if (!next())
        return false;
    }
    return next();
  }

  bool parseSingleQuotedStringInto(std::string &String) {
    if (!next())
      return false;
    while (*Position != '\'') {
      String.push_back(*Position);
      if (!next())
        return false;
    }
    return next();
  }

  bool parseFreeStringInto(std::string &String) {
    do {
      if (!skipEscapeCharacter())
        return false;
      String.push_back(*Position);
      if (!next())
        return false;
    } while (*Position != ' ' && *Position != '"' && *Position != '\'');
    return true;
  }

  bool skipEscapeCharacter() {
    if (*Position == '\\') {
      return next();
    }
    return true;
  }

  bool nextNonWhitespace() {
    do {
      if (!next())
        return false;
    } while (*Position == ' ');
    return true;
  }

  bool next() {
    ++Position;
    return Position != Input.end();
  }

  const StringRef Input;
  StringRef::iterator Position;
  std::vector<std::string> CommandLine;
};

} // namespace

std::vector<std::string> unescapeCommandLine(JSONCommandLineSyntax Syntax,
                                             StringRef EscapedCommandLine) {
  if (Syntax == JSONCommandLineSyntax::AutoDetect) {
#ifdef _WIN32
    // Assume Windows command line parsing on Win32
    Syntax = JSONCommandLineSyntax::Windows;
#else
    Syntax = JSONCommandLineSyntax::Gnu;
#endif
  }

  if (Syntax == JSONCommandLineSyntax::Windows) {
    llvm::BumpPtrAllocator Alloc;
    llvm::StringSaver Saver(Alloc);
    llvm::SmallVector<const char *, 64> T;
    llvm::cl::TokenizeWindowsCommandLine(EscapedCommandLine, Saver, T);
    std::vector<std::string> Result(T.begin(), T.end());
    return Result;
  }
  ENFORCE(Syntax == JSONCommandLineSyntax::Gnu,
          "Already handled other cases earlier");
  CommandLineArgumentParser parser(EscapedCommandLine);
  return parser.parse();
}

} // namespace scip_clang
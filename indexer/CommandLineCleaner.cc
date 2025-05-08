#include "llvm/ADT/StringRef.h"

#include "indexer/CommandLineCleaner.h"
#include "indexer/Enforce.h"

namespace {

enum class Action {
  Keep,
  ZapOne,
  ZapTwo,
};

template <typename T>
void zap(std::vector<T> &vs, absl::FunctionRef<Action(const T &)> check) {
  bool dropFromBefore = false;
  auto zappedBegin =
      std::stable_partition(vs.begin(), vs.end(), [&](const T &v) -> bool {
        if (dropFromBefore) {
          dropFromBefore = false;
          return false;
        }
        switch (check(v)) {
        case Action::Keep:
          return true;
        case Action::ZapOne:
          return false;
        case Action::ZapTwo:
          dropFromBefore = true;
          return false;
        }
      });
  vs.resize(std::distance(vs.begin(), zappedBegin));
}

// Strip out architecture specific flags, because scip-clang may
// be used to index code which relies on architectures known only
// to GCC, or only to some proprietary compilers.
constexpr const char *clangGccSkipOptionsWithArgs[] = {
    "-march",
    "-mcpu",
    "-mtune",
};

// Patterns of arg-less options to strip out.
//
// For example, Clang supports -mfix-cortex-a53-835769 (so does GCC)
// but GCC supports -mfix-cortex-a53-843419 which is not supported by Clang.
//
// In practice, options starting with '-m' seem to all correspond to
// ABI-related options (which ~never affect codenav). However, we cannot
// simply use '-m.*' as the pattern here, because some options with '-m'
// take an argument and some do not, and there isn't an easy programmatic
// way to determine which ones do/do not.
constexpr const char *clangGccSkipOptionsNoArgsPattern = "-m(no-)?fix-.*";

} // namespace

namespace scip_clang::compdb {

void CommandLineCleaner::clean(std::vector<std::string> &commandLine) const {
  zap<std::string>(commandLine, [this](const std::string &arg) -> Action {
    if (!arg.starts_with('-')) {
      return Action::Keep;
    }
    std::string_view flag = arg;
    auto eqIndex = arg.find('=');
    if (eqIndex != std::string::npos) {
      flag = std::string_view(arg.data(), eqIndex);
    } else if (this->noArgumentMatcher
               && this->noArgumentMatcher->match(llvm::StringRef(arg))) {
      return Action::ZapOne;
    }
    auto it = this->toZap.find(flag);
    if (it == this->toZap.end()) {
      return Action::Keep;
    }
    switch (it->second) {
    case CliOptionKind::NoArgument:
      return Action::ZapOne;
    case CliOptionKind::OneArgument:
      if (flag.size() == arg.size()) {
        return Action::ZapTwo;
      }
      return Action::ZapOne;
    }
    ENFORCE(false, "should've exited earlier");
  });
}

std::unique_ptr<CommandLineCleaner> CommandLineCleaner::forClangOrGcc() {
  CommandLineCleaner::MapType toZap;
  for (auto s : clangGccSkipOptionsWithArgs) {
    toZap.emplace(std::string_view(s), CliOptionKind::NoArgument);
  }
  CommandLineCleaner cleaner{
      .toZap = std::move(toZap),
      .noArgumentMatcher = {llvm::Regex(clangGccSkipOptionsNoArgsPattern)}};
  return std::make_unique<CommandLineCleaner>(std::move(cleaner));
}

} // namespace scip_clang::compdb
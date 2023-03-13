#include <string>

#include "clang/AST/Decl.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/LlvmAdapter.h"

namespace scip_clang {
namespace llvm_ext {

std::string formatDecl(const clang::Decl *decl) {
  if (!decl) {
    return "<null>";
  }
  std::string buf;
  llvm::raw_string_ostream out(buf);
  if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl)) {
    namedDecl->printQualifiedName(out);
  } else {
    decl->print(out);
  }
  return buf;
}

} // namespace llvm_ext
} // namespace scip_clang
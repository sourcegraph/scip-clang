#ifndef SCIP_CLANG_SYMBOL_FORMATTER_H
#define SCIP_CLANG_SYMBOL_FORMATTER_H

#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/StringSaver.h"

#include "scip/scip.pb.h"

#include "indexer/ClangAstMacros.h"
#include "indexer/Derive.h"
#include "indexer/FileMetadata.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"

namespace clang {
#define FORWARD_DECLARE(DeclName) class DeclName##Decl;
FOR_EACH_DECL_TO_BE_INDEXED(FORWARD_DECLARE)
#undef FORWARD_DECLARE

class Decl;
class DeclContext;
class NamedDecl;
class TagDecl;
class ValueDecl;
} // namespace clang

namespace llvm {
class raw_ostream;
}

namespace scip_clang {

/// Type similar to \c scip::Descriptor but carrying \c string_view fields
/// instead to avoid redundant intermediate allocations.
struct DescriptorBuilder {
  std::string_view name;
  std::string_view disambiguator;
  scip::Descriptor::Suffix suffix;

  void formatTo(llvm::raw_ostream &) const;
};

/// Type similar to \c scip::Symbol but carrying \c string_view fields instead
/// to avoid redundant allocations.
///
/// Meant for transient use in constructing symbol strings, since a
/// \c scip::Index doesn't store any \c scip::Symbol values directly.
struct SymbolBuilder {
  PackageId packageId;
  llvm::SmallVector<DescriptorBuilder, 4> descriptors;

  /// Format a symbol string according to the standardized SCIP representation:
  /// https://github.com/sourcegraph/scip/blob/main/scip.proto#L101-L127
  void formatTo(std::string &) const;

  /// Format the symbol string for an entity, making use of the symbol string
  /// for its declaration context.
  ///
  /// For example, when constructing a symbol string for \c std::string_view,
  /// \p contextSymbol would be the symbol string for \c std,
  /// and \p descriptor would describe the \c string_view type.
  ///
  /// Since the standard formatting for SCIP symbols is prefix-based,
  /// this avoids the extra work of recomputing parent symbol strings.
  static void formatContextual(std::string &buf, std::string_view contextSymbol,
                               const DescriptorBuilder &descriptor);
};

using SymbolString = std::string_view;

class SymbolFormatter final {
  const clang::SourceManager &sourceManager;
  FileMetadataMap &fileMetadataMap;

  llvm::BumpPtrAllocator bumpPtrAllocator;
  llvm::StringSaver stringSaver;

  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::SourceLocation>,
                      SymbolString>
      locationBasedCache;
  absl::flat_hash_map<const clang::Decl *, SymbolString> declBasedCache;
  absl::flat_hash_map<StableFileId, SymbolString> fileSymbolCache;
  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, uint32_t>
      anonymousTypeCounters;
  absl::flat_hash_map<llvm_ext::AbslHashAdapter<clang::FileID>, uint32_t>
      localVariableCounters;

  std::string scratchBufferForName;
  std::string scratchBufferForSymbol;

public:
  SymbolFormatter(const clang::SourceManager &sourceManager,
                  FileMetadataMap &fileMetadataMap)
      : sourceManager(sourceManager), fileMetadataMap(fileMetadataMap),
        bumpPtrAllocator(), stringSaver(bumpPtrAllocator), locationBasedCache(),
        declBasedCache(), fileSymbolCache(), localVariableCounters(),
        scratchBufferForName(), scratchBufferForSymbol() {}
  SymbolFormatter(const SymbolFormatter &) = delete;
  SymbolFormatter &operator=(const SymbolFormatter &) = delete;

  SymbolString getMacroSymbol(clang::SourceLocation defLoc);

  SymbolString getFileSymbol(const FileMetadata &);

#define DECLARE_GET_SYMBOL(DeclName)                 \
  std::optional<SymbolString> get##DeclName##Symbol( \
      const clang::DeclName##Decl &);
  FOR_EACH_DECL_TO_BE_INDEXED(DECLARE_GET_SYMBOL)
#undef DECLARE_GET_SYMBOL

  std::optional<SymbolString> getLocalVarOrParmSymbol(const clang::VarDecl &);

  std::optional<SymbolString> getNamedDeclSymbol(const clang::NamedDecl &);

  std::optional<SymbolString> getTagSymbol(const clang::TagDecl &);

private:
  std::optional<SymbolString> getContextSymbol(const clang::DeclContext &);

  std::optional<SymbolString> getNextLocalSymbol(const clang::NamedDecl &);

  std::optional<SymbolString>
  getSymbolCached(const clang::Decl &,
                  absl::FunctionRef<std::optional<SymbolString>()>);

  // --- Final step functions for symbol formatting ---

  /// Format a symbol for an entity isn't inside a namespace/type/etc. and isn't
  /// a local
  SymbolString format(const SymbolBuilder &);

  /// Format a symbol for an entity inside some namespace/type/etc.
  SymbolString formatContextual(std::string_view contextSymbol,
                                const DescriptorBuilder &descriptor);

  /// Format an entity which cannot be referenced outside the current file.
  SymbolString formatLocal(unsigned counter);

  // --- Intermediate functions for symbol formatting ---

  /// Format the string to a buffer stored by `this` and return a view to it.
  template <typename... T>
  std::string_view formatTemporaryName(fmt::format_string<T...> fmt,
                                       T &&...args) {
    return this->formatTemporaryToBuf(this->scratchBufferForName, fmt,
                                      std::forward<T>(args)...);
  }

  /// Format the name of the decl to a buffer stored by `this` and return a view
  /// to it.
  std::string_view formatTemporaryName(const clang::NamedDecl &);

  std::string_view getFunctionDisambiguator(const clang::FunctionDecl &,
                                            char[16]);

  template <typename... T>
  std::string_view formatTemporaryToBuf(std::string &buf,
                                        fmt::format_string<T...> fmt,
                                        T &&...args) const {
    buf.clear();
    fmt::format_to(std::back_inserter(buf), fmt, std::forward<T>(args)...);
    return std::string_view(buf);
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_SYMBOL_FORMATTER_H

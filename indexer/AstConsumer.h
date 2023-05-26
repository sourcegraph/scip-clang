#ifndef SCIP_CLANG_AST_CONSUMER_H
#define SCIP_CLANG_AST_CONSUMER_H

#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaConsumer.h"
#include "llvm/ADT/StringRef.h"

#include "proto/fwd_decls.pb.h"
#include "scip/scip.pb.h"

#include "indexer/IpcMessages.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Path.h"

namespace clang {
class CompilerInstance;
}

namespace scip_clang {
class ClangIdLookupMap;
class FileMetadataMap;
class IndexerPreprocessorWrapper;
class MacroIndexer;
class TuIndexer;
} // namespace scip_clang

namespace scip_clang {

/// Callback passed into the AST consumer so that it can decide
/// which files to index when traversing the translation unit.
///
/// The return value is true iff the indexing job should be run.
using WorkerCallback = absl::FunctionRef<bool(SemanticAnalysisJobResult &&,
                                              EmitIndexJobDetails &)>;

struct IndexerAstConsumerOptions {
  RootPath projectRootPath;
  RootPath buildRootPath;
  WorkerCallback getEmitIndexDetails;
  bool deterministic;
};

/// Type to track which files should be indexed.
///
/// For files that do not belong to this project; their symbols should be
/// tracked in external symbols instead of creating a \c scip::Document.
///
/// Not every file that is part of this project will be part of this map.
/// For example, if a file+hash was already indexed by another worker,
/// then one shouldn't call insert(..) for that file.
using FileIdsToBeIndexedSet =
    absl::flat_hash_set<llvm_ext::AbslHashAdapter<clang::FileID>>;

struct TuIndexingOutput {
  /// Index storing per-document output and external symbols
  /// for symbols that have definitions.
  scip::Index docsAndExternals;
  /// Index storing information about forward declarations.
  /// Only the external_symbols list is populated.
  scip::ForwardDeclIndex forwardDecls;

  TuIndexingOutput() = default;
  TuIndexingOutput(const TuIndexingOutput &) = delete;
  TuIndexingOutput &operator=(const TuIndexingOutput &) = delete;
};

class IndexerAstConsumer : public clang::SemaConsumer {
  const IndexerAstConsumerOptions &options;
  IndexerPreprocessorWrapper *preprocessorWrapper;
  clang::Sema *sema;
  TuIndexingOutput &tuIndexingOutput;

public:
  IndexerAstConsumer(clang::CompilerInstance &, llvm::StringRef /*filepath*/,
                     const IndexerAstConsumerOptions &options,
                     IndexerPreprocessorWrapper *preprocessorWrapper,
                     TuIndexingOutput &tuIndexingOutput);

  virtual void HandleTranslationUnit(clang::ASTContext &astContext) override;

  virtual void InitializeSema(clang::Sema &S) override;

  virtual void ForgetSema() override;

private:
  void computeFileIdsToBeIndexed(const clang::ASTContext &astContext,
                                 const EmitIndexJobDetails &emitIndexDetails,
                                 const ClangIdLookupMap &clangIdLookupMap,
                                 FileMetadataMap &fileMetadataMap,
                                 FileIdsToBeIndexedSet &toBeIndexed);

  void saveIncludeReferences(const FileIdsToBeIndexedSet &toBeIndexed,
                             const MacroIndexer &macroIndexer,
                             const ClangIdLookupMap &clangIdLookupMap,
                             const FileMetadataMap &fileMetadataMap,
                             TuIndexer &tuIndexer);
};

} // namespace scip_clang

#endif // SCIP_CLANG_AST_CONSUMER_H
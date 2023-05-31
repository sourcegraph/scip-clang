#include "absl/container/flat_hash_set.h"
#include "perfetto/perfetto.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Sema.h"

#include "indexer/AstConsumer.h"
#include "indexer/IdPathMappings.h"
#include "indexer/Indexer.h"
#include "indexer/LlvmAdapter.h"
#include "indexer/Preprocessing.h"
#include "indexer/SymbolFormatter.h"
#include "indexer/Tracing.h"

namespace scip_clang {
namespace {

class IndexerAstVisitor : public clang::RecursiveASTVisitor<IndexerAstVisitor> {
  using Base = RecursiveASTVisitor;

  const FileMetadataMap &fileMetadataMap;
  const FileIdsToBeIndexedSet &toBeIndexed;
  bool deterministic;

  TuIndexer &tuIndexer;

public:
  IndexerAstVisitor(const FileMetadataMap &fileMetadataMap,
                    const FileIdsToBeIndexedSet &toBeIndexed,
                    bool deterministic, TuIndexer &tuIndexer)
      : fileMetadataMap(fileMetadataMap), toBeIndexed(toBeIndexed),
        deterministic(deterministic), tuIndexer(tuIndexer) {}

  // See clang/include/clang/Basic/DeclNodes.td for list of declarations.

#define VISIT_DECL(DeclName)                                         \
  bool Visit##DeclName##Decl(clang::DeclName##Decl *decl) {          \
    ENFORCE(decl, "expected visitor to only access non-null decls"); \
    this->tuIndexer.save##DeclName##Decl(*decl);                     \
    return true;                                                     \
  }
  FOR_EACH_DECL_TO_BE_INDEXED(VISIT_DECL)
#undef VISIT_DECL

#define VISIT_EXPR(ExprName)                                     \
  bool Visit##ExprName##Expr(clang::ExprName##Expr *expr) {      \
    ENFORCE(expr, "expected " #ExprName "Expr to be null-null"); \
    this->tuIndexer.save##ExprName##Expr(*expr);                 \
    return true;                                                 \
  }
  FOR_EACH_EXPR_TO_BE_INDEXED(VISIT_EXPR)
#undef VISIT_EXPR

#define VISIT_TYPE_LOC(TypeName)                                    \
  bool Visit##TypeName##TypeLoc(clang::TypeName##TypeLoc typeLoc) { \
    this->tuIndexer.save##TypeName##TypeLoc(typeLoc);               \
    return true;                                                    \
  }
  FOR_EACH_TYPE_TO_BE_INDEXED(VISIT_TYPE_LOC)
#undef VISIT_TYPE_LOC

  /// Unlike many other entities, there is no corresponding Visit* method in
  /// RecursiveTypeVisitor, so override the Traverse* method instead.
  bool TraverseNestedNameSpecifierLoc(
      const clang::NestedNameSpecifierLoc nestedNameSpecifierLoc) {
    if (nestedNameSpecifierLoc) {
      this->tuIndexer.saveNestedNameSpecifierLoc(nestedNameSpecifierLoc);
    }
    return true;
  }

#define TRY_TO(CALL_EXPR)              \
  do {                                 \
    if (!this->getDerived().CALL_EXPR) \
      return false;                    \
  } while (false)

  /// Replace the default implementation of the Traverse* method as there
  /// is no matching Visit* method, and the default implementation
  /// does not visit member field references.
  /// See https://github.com/llvm/llvm-project/issues/61602
  bool TraverseConstructorInitializer(
      const clang::CXXCtorInitializer *cxxCtorInitializer) {
    if (clang::TypeSourceInfo *TInfo =
            cxxCtorInitializer->getTypeSourceInfo()) {
      TRY_TO(TraverseTypeLoc(TInfo->getTypeLoc()));
    }
    if (clang::FieldDecl *fieldDecl = cxxCtorInitializer->getAnyMember()) {
      this->tuIndexer.saveFieldReference(
          *fieldDecl, cxxCtorInitializer->getSourceLocation());
    }
    if (cxxCtorInitializer->isWritten()
        || this->getDerived().shouldVisitImplicitCode()) {
      TRY_TO(TraverseStmt(cxxCtorInitializer->getInit()));
    }
    return true;
  }
#undef TRY_TO

  void writeIndex(SymbolFormatter &&symbolFormatter, MacroIndexer &&macroIndex,
                  TuIndexingOutput &tuIndexingOutput) {
    std::vector<std::pair<RootRelativePathRef, clang::FileID>>
        indexedProjectFiles;
    for (auto wrappedFileId : this->toBeIndexed) {
      if (auto optStableFileId =
              this->fileMetadataMap.getStableFileId(wrappedFileId.data)) {
        if (optStableFileId->isInProject) {
          indexedProjectFiles.emplace_back(optStableFileId->path,
                                           wrappedFileId.data);
        }
      }
    }
    TRACE_EVENT(tracing::indexIo, "IndexerAstVisitor::writeIndex", "fileCount",
                indexedProjectFiles.size());

    if (this->deterministic) {
      auto comparePaths = [](const auto &p1, const auto &p2) -> bool {
        auto cmp = p1.first <=> p2.first;
        ENFORCE(cmp != 0,
                "document with path '{}' is present 2+ times in index",
                p1.first.asStringView());
        return cmp == std::strong_ordering::less;
      };
      absl::c_sort(indexedProjectFiles, comparePaths);
    }

    for (auto [relPathRef, fileId] : indexedProjectFiles) {
      scip::Document document;
      auto relPath = relPathRef.asStringView();
      document.set_relative_path(relPath.data(), relPath.size());
      // FIXME(def: set-language): Use Clang's built-in detection logic here.
      // Q: With Clang's built-in language detection, does the built-in fake
      // header differ between C, C++ and Obj-C (it presumably should?)?
      // Otherwise, do we need to mix in the language into the hash?
      // Or do we fall back to the common denominator (= C)?
      // Or should we add an other_languages in SCIP?
      document.set_language(scip::Language_Name(scip::Language::CPP));
      macroIndex.emitDocumentOccurrencesAndSymbols(
          this->deterministic, symbolFormatter, fileId, document);
      this->tuIndexer.emitDocumentOccurrencesAndSymbols(this->deterministic,
                                                        fileId, document);
      *tuIndexingOutput.docsAndExternals.add_documents() = std::move(document);
    }
    this->tuIndexer.emitExternalSymbols(deterministic,
                                        tuIndexingOutput.docsAndExternals);
    this->tuIndexer.emitForwardDeclarations(deterministic,
                                            tuIndexingOutput.forwardDecls);
    macroIndex.emitExternalSymbols(this->deterministic, symbolFormatter,
                                   tuIndexingOutput.docsAndExternals);
  }

  // For the various hierarchies, see clang/Basic/.*.td files
  // https://sourcegraph.com/search?q=context:global+repo:llvm/llvm-project%24+file:clang/Basic/.*.td&patternType=standard&sm=1&groupBy=repo
};

} // namespace

IndexerAstConsumer::IndexerAstConsumer(
    clang::CompilerInstance &, llvm::StringRef /*filepath*/,
    const IndexerAstConsumerOptions &options,
    IndexerPreprocessorWrapper *preprocessorWrapper,
    TuIndexingOutput &tuIndexingOutput)
    : options(options), preprocessorWrapper(preprocessorWrapper), sema(nullptr),
      tuIndexingOutput(tuIndexingOutput) {}

// virtual override
void IndexerAstConsumer::HandleTranslationUnit(clang::ASTContext &astContext) {
  // NOTE(ref: preprocessor-traversal-ordering): The call order is
  // 1. The preprocessor wrapper finishes running.
  // 2. This function is called.
  // 3. EndOfMainFile is called in the preprocessor wrapper.
  // 4. The preprocessor wrapper is destroyed.
  //
  // So flush the state from the wrapper in this function, and use
  // it during the traversal (instead of say flushing state in the dtor
  // would arguably be more idiomatic).
  SemanticAnalysisJobResult semaResult{};
  ClangIdLookupMap clangIdLookupMap{};
  auto &sourceManager = astContext.getSourceManager();
  MacroIndexer macroIndexer{sourceManager};
  this->preprocessorWrapper->flushState(semaResult, clangIdLookupMap,
                                        macroIndexer);

  EmitIndexJobDetails emitIndexDetails{};
  bool shouldEmitIndex = this->options.getEmitIndexDetails(
      std::move(semaResult), emitIndexDetails);
  if (!shouldEmitIndex) {
    return;
  }

  FileMetadataMap fileMetadataMap{this->options.projectRootPath,
                                  this->options.buildRootPath,
                                  this->options.packageMap};
  FileIdsToBeIndexedSet toBeIndexed{};
  this->computeFileIdsToBeIndexed(astContext, emitIndexDetails,
                                  clangIdLookupMap, fileMetadataMap,
                                  toBeIndexed);

  toBeIndexed.insert({astContext.getSourceManager().getMainFileID()});

  SymbolFormatter symbolFormatter{sourceManager, fileMetadataMap};
  TuIndexer tuIndexer{
      sourceManager, this->sema->getLangOpts(), this->sema->getASTContext(),
      toBeIndexed,   symbolFormatter,           fileMetadataMap};

  this->saveIncludeReferences(toBeIndexed, macroIndexer, clangIdLookupMap,
                              fileMetadataMap, tuIndexer);

  IndexerAstVisitor visitor{fileMetadataMap, toBeIndexed,
                            this->options.deterministic, tuIndexer};
  visitor.TraverseAST(astContext);

  visitor.writeIndex(std::move(symbolFormatter), std::move(macroIndexer),
                     this->tuIndexingOutput);
}

// virtual override
void IndexerAstConsumer::InitializeSema(clang::Sema &S) {
  this->sema = &S;
}

// virtual override
void IndexerAstConsumer::ForgetSema() {
  this->sema = nullptr;
}

void IndexerAstConsumer::computeFileIdsToBeIndexed(
    const clang::ASTContext &astContext,
    const EmitIndexJobDetails &emitIndexDetails,
    const ClangIdLookupMap &clangIdLookupMap, FileMetadataMap &fileMetadataMap,
    FileIdsToBeIndexedSet &toBeIndexed) {
  auto &sourceManager = astContext.getSourceManager();
  auto mainFileId = sourceManager.getMainFileID();

  fileMetadataMap.populate(clangIdLookupMap);
  if (auto *mainFileEntry = sourceManager.getFileEntryForID(mainFileId)) {
    if (auto optMainFileAbsPath =
            AbsolutePathRef::tryFrom(mainFileEntry->tryGetRealPathName())) {
      fileMetadataMap.insert(mainFileId, optMainFileAbsPath.value());
      toBeIndexed.insert({mainFileId});
    } else {
      spdlog::debug(
          "tryGetRealPathName() returned non-absolute path '{}'",
          llvm_ext::toStringView(mainFileEntry->tryGetRealPathName()));
    }
  }

  for (auto &fileInfo : emitIndexDetails.filesToBeIndexed) {
    auto absPathRef = fileInfo.path.asRef();
    auto optFileId = clangIdLookupMap.lookup(absPathRef, fileInfo.hashValue);
    if (!optFileId.has_value()) {
      spdlog::debug(
          "failed to find clang::FileID for path '{}' received from Driver",
          absPathRef.asStringView());
      continue;
    }
    toBeIndexed.insert({*optFileId});
  }
}

void IndexerAstConsumer::saveIncludeReferences(
    const FileIdsToBeIndexedSet &toBeIndexed, const MacroIndexer &macroIndexer,
    const ClangIdLookupMap &clangIdLookupMap,
    const FileMetadataMap &fileMetadataMap, TuIndexer &tuIndexer) {
  for (auto &wrappedFileId : toBeIndexed) {
    if (auto *fileMetadata =
            fileMetadataMap.getFileMetadata(wrappedFileId.data)) {
      tuIndexer.saveSyntheticFileDefinition(wrappedFileId.data, *fileMetadata);
    }
    macroIndexer.forEachIncludeInFile(
        wrappedFileId.data,
        [&](clang::SourceRange range, AbsolutePathRef importedFilePath) {
          auto optRefFileId =
              clangIdLookupMap.lookupAnyFileId(importedFilePath);
          if (!optRefFileId.has_value()) {
            return;
          }
          auto refFileId = *optRefFileId;
          auto *fileMetadata = fileMetadataMap.getFileMetadata(*optRefFileId);
          ENFORCE(fileMetadata,
                  "missing FileMetadata value for path {} (FileID = {})",
                  importedFilePath.asStringView(), refFileId.getHashValue());
          tuIndexer.saveInclude(range, *fileMetadata);
        });
  }
}

} // namespace scip_clang
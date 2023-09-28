#ifndef SCIP_CLANG_COMPILATION_DATABASE_H
#define SCIP_CLANG_COMPILATION_DATABASE_H

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "indexer/Enforce.h" // defines ENFORCE used by rapidjson headers

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/reader.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "llvm/Support/Regex.h"

#include "indexer/Derive.h"
#include "indexer/FileSystem.h"

namespace clang::tooling {
struct CompileCommand;
} // namespace clang::tooling

namespace scip_clang {

class AbsolutePath;

namespace compdb {

struct ValidationOptions {
  bool checkDirectoryPathsAreAbsolute;
  bool tryDetectOutOfProjectRoot;
};

class File {
  size_t _sizeInBytes;
  size_t _commandCount;

public:
  FILE *file;

  static File openAndExitOnErrors(const StdPath &, ValidationOptions);

  size_t sizeInBytes() const {
    return this->_sizeInBytes;
  }
  size_t commandCount() const {
    return this->_commandCount;
  }

private:
  static File open(const StdPath &, ValidationOptions,
                   std::error_code &fileSizeError);
};

// Key to identify fields in a command object
enum class Key : uint32_t {
  Unset = 0,
  Directory = 1 << 1,
  File = 1 << 2,
  Arguments = 1 << 3,
  Command = 1 << 4,
  Output = 1 << 5,
};

/// The 'command object' terminology is taken from the official Clang docs.
/// https://clang.llvm.org/docs/JSONCompilationDatabase.html
struct CommandObject {
  static constexpr size_t POISON_INDEX = 8080808080;

  size_t index = /*poison value*/ POISON_INDEX;
  /// Strictly speaking, this should be an absolute directory in an actual
  /// compilation database (see NOTE(ref: directory-field-is-absolute)),
  /// but we use a std::string instead as it may be a relative path for
  /// test cases.
  std::string workingDirectory;
  // May be relative or absolute
  std::string filePath;
  std::vector<std::string> arguments;
};
SERIALIZABLE(CommandObject)

// Handler for extracting command objects from compilation database.
class CommandObjectHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          CommandObjectHandler> {
  compdb::Key previousKey;
  compdb::CommandObject wipCommand;
  size_t parseLimit;

public:
  std::vector<compdb::CommandObject> commands;

  CommandObjectHandler(size_t parseLimit)
      : previousKey(Key::Unset), wipCommand(), parseLimit(parseLimit),
        commands() {}

  bool String(const char *str, rapidjson::SizeType length, bool copy);
  bool Key(const char *str, rapidjson::SizeType length, bool copy);
  bool EndObject(rapidjson::SizeType memberCount);

  bool reachedLimit() const;
};

enum class CompilerKind {
  Gcc,
  Clang,
  Nvcc,
};

struct ToolchainInfo {
  /// Purely for debugging.
  virtual CompilerKind kind() const = 0;

  /// Potentially logs some error messages and/or warnings.
  /// The method should return true iff some errors are logged.
  virtual bool isWellFormed() const = 0;

  virtual void
  adjustCommandLine(std::vector<std::string> &commandLine) const = 0;

  virtual ~ToolchainInfo() = default;

  /// Attempt to determine the toolchain information based on the path
  /// to a compiler or a compiler wrapper (e.g. from Bazel or when using
  /// ccache).
  ///
  /// Returns nullptr if we failed to create a well-formed toolchain object.
  static std::unique_ptr<ToolchainInfo>
  infer(const scip_clang::AbsolutePath &compilerPath);
};

/// The settings used to customize the parsed results generated from
/// the compilation database.
///
/// Ideally, most of the options here would live in an extra 'semantic'
/// layer that sat on top of a parser. However, since the database is
/// parsed in a streaming fashion, chaining the two layers would be
/// a bit more fiddly from the caller side, so we keep the filtering
/// options in the parser itself.
struct ParseOptions {
  size_t refillCount;
  bool adjustCommandLine;
  bool skipNonMainFileEntries;
  bool checkFilesExist;

  static ParseOptions create(size_t refillCount, bool forTesting = false);
};

struct ParseStats {
  unsigned skippedNonTuFileExtension = 0;
  unsigned skippedNonExistentTuFile = 0;
};

class ResumableParser {
  std::string jsonStreamBuffer;
  std::optional<rapidjson::FileReadStream> compDbStream;
  std::optional<CommandObjectHandler> handler;
  rapidjson::Reader reader;
  size_t currentIndex = 0;

  ParseOptions options;
  absl::flat_hash_set<std::string> emittedErrors;

  llvm::Regex fileExtensionRegex;

  /// Mapping from compiler/wrapper path (the first element of argv
  /// as in the compilation database) to information about the toolchain
  /// needed to tweak the command object before invoking the Clang driver.
  ///
  /// For example, Bazel uses a compiler wrapper, but scip-clang needs
  /// to use the full path to the compiler driver when running semantic
  /// analysis, so that include directories are picked up correctly
  /// relative to the driver's location.
  ///
  /// In case there is a failure to determine the toolchain information,
  /// a null value is stored for the unique_ptr.
  absl::flat_hash_map<std::string, std::unique_ptr<ToolchainInfo>>
      toolchainInfoMap;

public:
  ParseStats stats;

  ResumableParser() = default;
  ResumableParser(const ResumableParser &) = delete;
  ResumableParser &operator=(const ResumableParser &) = delete;

  void initialize(compdb::File compdb, ParseOptions);

  /// Parses at most \c options.refillCount elements into \param out.
  void parseMore(std::vector<CommandObject> &out);

private:
  void adjustCommandLine(const std::string &directoryPath,
                         std::vector<std::string> &commandLine);
  void emitError(std::string &&error);
};

} // namespace compdb
} // namespace scip_clang

#endif // SCIP_CLANG_COMPILATION_DATABASE_H
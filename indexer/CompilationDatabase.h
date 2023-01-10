#ifndef SCIP_CLANG_COMPILATION_DATABASE_H
#define SCIP_CLANG_COMPILATION_DATABASE_H

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "indexer/Enforce.h" // defines ENFORCE used by rapidjson headers

#include "absl/container/flat_hash_set.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/reader.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "clang/Tooling/CompilationDatabase.h"

#include "indexer/FileSystem.h"

namespace scip_clang {
namespace compdb {

struct CompilationDatabaseFile {
  FILE *file;
  size_t sizeInBytes;
  size_t commandCount;

  static CompilationDatabaseFile open(const StdPath &path, std::error_code &ec);
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

// Handler for extracting command objects from compilation database.
class CommandObjectHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          CommandObjectHandler> {
  compdb::Key previousKey;
  clang::tooling::CompileCommand wipCommand;
  size_t parseLimit;

public:
  std::vector<clang::tooling::CompileCommand> commands;

  CommandObjectHandler(size_t parseLimit)
      : previousKey(Key::Unset), wipCommand(), parseLimit(parseLimit),
        commands() {}

  bool String(const char *str, rapidjson::SizeType length, bool copy);
  bool Key(const char *str, rapidjson::SizeType length, bool copy);
  bool EndObject(rapidjson::SizeType memberCount);

  bool reachedLimit() const;
};

class ResumableParser {
  std::string jsonStreamBuffer;
  std::optional<rapidjson::FileReadStream> compDbStream;
  std::optional<CommandObjectHandler> handler;
  rapidjson::Reader reader;

public:
  ResumableParser() = default;
  ResumableParser(const ResumableParser &) = delete;
  ResumableParser &operator=(const ResumableParser &) = delete;

  void initialize(CompilationDatabaseFile compdb, size_t refillCount);

  // Parses at most refillCount elements (passed during initialization)
  // from the compilation database passed during initialization.
  void parseMore(std::vector<clang::tooling::CompileCommand> &out);
};

} // namespace compdb
} // namespace scip_clang

#endif // SCIP_CLANG_COMPILATION_DATABASE_H
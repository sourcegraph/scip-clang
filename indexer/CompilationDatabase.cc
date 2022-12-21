#include <string>

#include "rapidjson/error/en.h"
#include "rapidjson/rapidjson.h"

#include "indexer/CompilationDatabase.h"
#include "indexer/LLVMCommandLineParsing.h"

namespace scip_clang {

namespace compdb {

bool CommandObjectHandler::String(const char *str, rapidjson::SizeType length,
                                  bool copy) {
  switch (this->previousKey) {
  case Key::Unset:
    // FIXME(ref: add-enforce)
    assert(false && "Unexpected input");
    return false;
  case Key::Directory:
    this->wipCommand.Directory = std::string(str, length);
    break;
  case Key::File:
    this->wipCommand.Filename = std::string(str, length);
    break;
  case Key::Command:
    this->wipCommand.CommandLine = scip_clang::unescapeCommandLine(
        clang::tooling::JSONCommandLineSyntax::AutoDetect,
        std::string_view(str, length));
    break;
  case Key::Arguments: // Validator makes sure we have an array outside.
    this->wipCommand.CommandLine.emplace_back(str, length);
    break;
  case Key::Output:
    this->wipCommand.Output = std::string(str, length);
    break;
  }
  return true;
}

bool CommandObjectHandler::Key(const char *str, rapidjson::SizeType length,
                               bool copy) {
  auto key = std::string_view(str, length);
  if (key == "directory") {
    this->previousKey = Key::Directory;
  } else if (key == "file") {
    this->previousKey = Key::File;
  } else if (key == "command") {
    this->previousKey = Key::Command;
  } else if (key == "output") {
    this->previousKey = Key::Output;
  } else if (key == "arguments") {
    this->previousKey = Key::Arguments;
  } else {
    // FIXME(ref: add-enforce)
    assert(false && "unexpected key should've been caught by validation");
  }
  return true;
}

bool CommandObjectHandler::EndObject(rapidjson::SizeType memberCount) {
  this->commands.emplace_back(std::move(this->wipCommand));
  this->wipCommand = {};
  this->previousKey = Key::Unset;
  return true;
}

bool CommandObjectHandler::reachedLimit() const {
  return this->commands.size() == this->parseLimit;
}

void ResumableParser::initialize(size_t fileSize, size_t totalJobs,
                                 FILE *compDbFile, size_t refillCount) {
  auto averageJobSize = fileSize / totalJobs;
  // Some customers have averageJobSize = 150KiB.
  // If numWorkers == 300 (very high core count machine),
  // then the computed hint will be ~88MiB. The 128MiB is rounded up from 88MiB.
  // The fudge factor of 2 is to allow for oversized jobs.
  auto bufferSize =
      std::min(size_t(128 * 1024 * 1024), averageJobSize * 2 * refillCount);
  std::fseek(compDbFile, 0, SEEK_SET);
  this->handler = CommandObjectHandler(refillCount);
  this->jsonStreamBuffer.resize(bufferSize);
  this->compDbStream = rapidjson::FileReadStream(
      compDbFile, this->jsonStreamBuffer.data(), this->jsonStreamBuffer.size());
  this->reader.IterativeParseInit();
}

void ResumableParser::parseMore(
    std::vector<clang::tooling::CompileCommand> &out) {
  if (this->reader.IterativeParseComplete()) {
    if (this->reader.HasParseError()) {
      spdlog::error(
          "parse error: {} at offset {}",
          rapidjson::GetParseError_En(this->reader.GetParseErrorCode()),
          this->reader.GetErrorOffset());
    }
    return;
  }
  assert(this->handler && "missed call to initialize");
  assert(this->compDbStream && "missed call to initialize");

  while (!this->handler->reachedLimit()
         && !this->reader.IterativeParseComplete()) {
    this->reader.IterativeParseNext<rapidjson::kParseIterativeFlag>(
        this->compDbStream.value(), this->handler.value());
  }
  for (auto &cmd : this->handler->commands) {
    out.emplace_back(std::move(cmd));
  }
  this->handler->commands.clear();
}

} // namespace compdb

} // namespace scip_clang
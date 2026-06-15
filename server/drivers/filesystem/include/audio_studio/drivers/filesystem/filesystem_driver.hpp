#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::filesystem {

struct FileInfo {
  std::string path;
  uint64_t size = 0;
  bool directory = false;
};

class FileSystemDriver {
public:
  framework::Status writeText(const std::string& path, std::string content);
  framework::Status readText(const std::string& path, std::string& out) const;
  framework::Status exists(const std::string& path, bool& out) const;
  framework::Status remove(const std::string& path);
  framework::Status createDirectory(const std::string& path);
  framework::Status listDirectory(const std::string& path, std::vector<FileInfo>& out) const;
  std::string joinPath(const std::vector<std::string>& parts) const;

private:
  static bool isDirectChild(const std::string& parent, const std::string& child);

  std::map<std::string, std::string> files_;
  std::set<std::string> directories_;
};

} // namespace audio_studio::drivers::filesystem

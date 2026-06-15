#include "audio_studio/drivers/filesystem/filesystem_driver.hpp"

#include <utility>

namespace audio_studio::drivers::filesystem {

framework::Status FileSystemDriver::writeText(const std::string& path, std::string content) {
  if (path.empty()) return framework::Status::invalidArgument("file path is empty");
  files_[path] = std::move(content);
  return framework::Status::success();
}

framework::Status FileSystemDriver::readText(const std::string& path, std::string& out) const {
  const auto it = files_.find(path);
  if (it == files_.end()) return framework::Status::unavailable("file not found: " + path);
  out = it->second;
  return framework::Status::success();
}

framework::Status FileSystemDriver::exists(const std::string& path, bool& out) const {
  out = files_.find(path) != files_.end() || directories_.find(path) != directories_.end();
  return framework::Status::success();
}

framework::Status FileSystemDriver::remove(const std::string& path) {
  const auto files_erased = files_.erase(path);
  const auto dirs_erased = directories_.erase(path);
  if (files_erased == 0 && dirs_erased == 0) return framework::Status::unavailable("path not found: " + path);
  return framework::Status::success();
}

framework::Status FileSystemDriver::createDirectory(const std::string& path) {
  if (path.empty()) return framework::Status::invalidArgument("directory path is empty");
  directories_.insert(path);
  return framework::Status::success();
}

framework::Status FileSystemDriver::listDirectory(const std::string& path, std::vector<FileInfo>& out) const {
  if (directories_.find(path) == directories_.end()) return framework::Status::unavailable("directory not found: " + path);
  out.clear();
  for (const auto& directory : directories_) {
    if (directory != path && isDirectChild(path, directory)) out.push_back({directory, 0, true});
  }
  for (const auto& file : files_) {
    if (isDirectChild(path, file.first)) out.push_back({file.first, static_cast<uint64_t>(file.second.size()), false});
  }
  return framework::Status::success();
}

std::string FileSystemDriver::joinPath(const std::vector<std::string>& parts) const {
  std::string out;
  for (const auto& part : parts) {
    if (part.empty()) continue;
    if (!out.empty() && out.back() != '/') out.push_back('/');
    out += part;
  }
  return out;
}

bool FileSystemDriver::isDirectChild(const std::string& parent, const std::string& child) {
  const std::string prefix = parent.empty() || parent.back() == '/' ? parent : parent + "/";
  if (child.find(prefix) != 0) return false;
  const auto rest = child.substr(prefix.size());
  return !rest.empty() && rest.find('/') == std::string::npos;
}

} // namespace audio_studio::drivers::filesystem

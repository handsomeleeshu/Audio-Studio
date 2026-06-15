#include "linux_host_filesystem_driver.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace audio_studio::drivers::filesystem {

LinuxHostFile::LinuxHostFile(LinuxHostFileSystemDriver& driver) : driver_(driver) {}

DriverResult LinuxHostFile::open(const std::string& path, const FileOpenOptions& options) {
  if (path.empty()) return DriverResult::invalidArgument("file path is empty");
  const auto normalized = driver_.normalizePath(path);
  const bool exists = driver_.files_.find(normalized) != driver_.files_.end();
  if (!exists && !options.create) return DriverResult::unavailable("file not found: " + normalized);
  path_ = normalized;
  options_ = options;
  if (options.create && !exists) driver_.files_[path_] = {};
  if (options.truncate) driver_.files_[path_].clear();
  offset_ = 0;
  open_ = true;
  return DriverResult::success();
}

DriverResult LinuxHostFile::read(void* buffer, size_t capacity, size_t& read_bytes) {
  if (!open_) return DriverResult::unavailable("file is not open");
  if (!options_.read) return DriverResult::invalidArgument("file was not opened for read");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("file read buffer is null");
  const auto& content = driver_.files_[path_];
  const auto available = offset_ < content.size() ? content.size() - static_cast<size_t>(offset_) : 0;
  read_bytes = std::min(capacity, available);
  if (read_bytes > 0) std::memcpy(buffer, content.data() + offset_, read_bytes);
  offset_ += read_bytes;
  return DriverResult::success();
}

DriverResult LinuxHostFile::write(const void* data, size_t size, size_t& written_bytes) {
  if (!open_) return DriverResult::unavailable("file is not open");
  if (!options_.write) return DriverResult::invalidArgument("file was not opened for write");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("file write buffer is null");
  auto& content = driver_.files_[path_];
  if (offset_ > content.size()) content.resize(static_cast<size_t>(offset_), '\0');
  if (offset_ + size > content.size()) content.resize(static_cast<size_t>(offset_) + size);
  if (size > 0) std::memcpy(&content[static_cast<size_t>(offset_)], data, size);
  offset_ += size;
  written_bytes = size;
  return DriverResult::success();
}

DriverResult LinuxHostFile::seek(uint64_t offset) {
  if (!open_) return DriverResult::unavailable("file is not open");
  offset_ = offset;
  return DriverResult::success();
}

DriverResult LinuxHostFile::tell(uint64_t& offset) {
  if (!open_) return DriverResult::unavailable("file is not open");
  offset = offset_;
  return DriverResult::success();
}

DriverResult LinuxHostFile::flush() {
  if (!open_) return DriverResult::unavailable("file is not open");
  return DriverResult::success();
}

void LinuxHostFile::close() {
  open_ = false;
  path_.clear();
  offset_ = 0;
}

bool LinuxHostFile::isOpen() const {
  return open_;
}

uint64_t LinuxHostFile::size() const {
  const auto it = driver_.files_.find(path_);
  return it == driver_.files_.end() ? 0 : static_cast<uint64_t>(it->second.size());
}

std::unique_ptr<IFile> LinuxHostFileSystemDriver::createFile() {
  return std::make_unique<LinuxHostFile>(*this);
}

DriverResult LinuxHostFileSystemDriver::exists(const std::string& path, bool& result) {
  const auto normalized = normalizePath(path);
  result = files_.find(normalized) != files_.end() || directories_.find(normalized) != directories_.end();
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::remove(const std::string& path) {
  const auto normalized = normalizePath(path);
  const auto files_erased = files_.erase(normalized);
  const auto dirs_erased = directories_.erase(normalized);
  if (files_erased == 0 && dirs_erased == 0) return DriverResult::unavailable("path not found: " + normalized);
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::rename(const std::string& from, const std::string& to) {
  const auto normalized_from = normalizePath(from);
  const auto normalized_to = normalizePath(to);
  auto file = files_.find(normalized_from);
  if (file != files_.end()) {
    files_[normalized_to] = std::move(file->second);
    files_.erase(file);
    return DriverResult::success();
  }
  auto directory = directories_.find(normalized_from);
  if (directory != directories_.end()) {
    directories_.erase(directory);
    directories_.insert(normalized_to);
    return DriverResult::success();
  }
  return DriverResult::unavailable("path not found: " + normalized_from);
}

DriverResult LinuxHostFileSystemDriver::createDirectory(const std::string& path, bool /*recursive*/) {
  if (path.empty()) return DriverResult::invalidArgument("directory path is empty");
  directories_.insert(normalizePath(path));
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::listDirectory(const std::string& path, std::vector<FileInfo>& entries) {
  const auto normalized = normalizePath(path);
  if (directories_.find(normalized) == directories_.end()) return DriverResult::unavailable("directory not found: " + normalized);
  entries.clear();
  for (const auto& directory : directories_) {
    if (directory != normalized && isDirectChild(normalized, directory)) entries.push_back({directory, 0, true});
  }
  for (const auto& file : files_) {
    if (isDirectChild(normalized, file.first)) entries.push_back({file.first, static_cast<uint64_t>(file.second.size()), false});
  }
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::stat(const std::string& path, FileInfo& info) {
  const auto normalized = normalizePath(path);
  auto file = files_.find(normalized);
  if (file != files_.end()) {
    info = {normalized, static_cast<uint64_t>(file->second.size()), false};
    return DriverResult::success();
  }
  if (directories_.find(normalized) != directories_.end()) {
    info = {normalized, 0, true};
    return DriverResult::success();
  }
  return DriverResult::unavailable("path not found: " + normalized);
}

std::string LinuxHostFileSystemDriver::joinPath(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& part : parts) {
    if (part.empty()) continue;
    if (!out.empty() && out.back() != '/') out.push_back('/');
    out += part;
  }
  return out;
}

std::string LinuxHostFileSystemDriver::normalizePath(const std::string& path) {
  if (path.empty()) return "/";
  std::string out;
  bool last_was_slash = false;
  for (char ch : path) {
    if (ch == '/') {
      if (!last_was_slash) out.push_back(ch);
      last_was_slash = true;
    } else {
      out.push_back(ch);
      last_was_slash = false;
    }
  }
  if (out.size() > 1 && out.back() == '/') out.pop_back();
  return out.empty() ? "/" : out;
}

std::string LinuxHostFileSystemDriver::absolutePath(const std::string& path) {
  const auto normalized = normalizePath(path);
  if (!normalized.empty() && normalized.front() == '/') return normalized;
  return normalizePath("/" + normalized);
}

bool LinuxHostFileSystemDriver::isDirectChild(const std::string& parent, const std::string& child) {
  const std::string prefix = parent.empty() || parent.back() == '/' ? parent : parent + "/";
  if (child.find(prefix) != 0) return false;
  const auto rest = child.substr(prefix.size());
  return !rest.empty() && rest.find('/') == std::string::npos;
}

} // namespace audio_studio::drivers::filesystem

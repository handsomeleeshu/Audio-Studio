#include "linux_host_filesystem_driver.hpp"

#include <filesystem>
#include <ios>
#include <system_error>
#include <utility>

namespace audio_studio::drivers::filesystem {

namespace {

class LinuxHostFileSystemDriverFactory final : public IFileSystemDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IFileSystemDriver> create() const override { return std::make_unique<LinuxHostFileSystemDriver>(); }
};

const bool kLinuxHostFileSystemDriverRegistered = [] {
  auto status = FileSystemDriverRegistry::instance().registerFactory(std::make_unique<LinuxHostFileSystemDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

DriverResult LinuxHostFile::open(const std::string& path, const FileOpenOptions& options) {
  if (path.empty()) return DriverResult::invalidArgument("file path is empty");
  if (!options.read && !options.write) return DriverResult::invalidArgument("file open requires read or write access");

  std::error_code ec;
  const auto absolute_path = std::filesystem::absolute(path, ec).lexically_normal();
  if (ec) return LinuxHostFileSystemDriver::filesystemError("resolve file path", path);

  const bool exists = std::filesystem::exists(absolute_path, ec);
  if (ec) return LinuxHostFileSystemDriver::filesystemError("check file", absolute_path.string());
  if (!exists && !options.create) return DriverResult::unavailable("file not found: " + absolute_path.string());

  if (options.create && !exists) {
    std::ofstream created(absolute_path, std::ios::binary);
    if (!created) return LinuxHostFileSystemDriver::filesystemError("create file", absolute_path.string());
  }

  std::ios::openmode mode = std::ios::binary;
  if (options.read) mode |= std::ios::in;
  if (options.write) mode |= std::ios::out;
  if (options.truncate) {
    if (!options.write) return DriverResult::invalidArgument("file truncate requires write access");
    mode |= std::ios::trunc;
  }

  stream_.open(absolute_path, mode);
  if (!stream_.is_open()) return LinuxHostFileSystemDriver::filesystemError("open file", absolute_path.string());

  path_ = absolute_path.string();
  options_ = options;
  offset_ = 0;
  return DriverResult::success();
}

DriverResult LinuxHostFile::read(void* buffer, size_t capacity, size_t& read_bytes) {
  read_bytes = 0;
  if (!stream_.is_open()) return DriverResult::unavailable("file is not open");
  if (!options_.read) return DriverResult::invalidArgument("file was not opened for read");
  if (buffer == nullptr && capacity > 0) return DriverResult::invalidArgument("file read buffer is null");

  if (capacity == 0) return DriverResult::success();

  stream_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(capacity));
  read_bytes = static_cast<size_t>(stream_.gcount());
  if (stream_.bad()) return DriverResult::internal("failed to read file: " + path_);
  stream_.clear();
  offset_ += read_bytes;
  return DriverResult::success();
}

DriverResult LinuxHostFile::write(const void* data, size_t size, size_t& written_bytes) {
  written_bytes = 0;
  if (!stream_.is_open()) return DriverResult::unavailable("file is not open");
  if (!options_.write) return DriverResult::invalidArgument("file was not opened for write");
  if (data == nullptr && size > 0) return DriverResult::invalidArgument("file write buffer is null");

  if (size > 0) {
    stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!stream_) return DriverResult::internal("failed to write file: " + path_);
  }
  written_bytes = size;
  offset_ += written_bytes;
  return DriverResult::success();
}

DriverResult LinuxHostFile::seek(uint64_t offset) {
  if (!stream_.is_open()) return DriverResult::unavailable("file is not open");
  stream_.clear();
  if (options_.read) stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (options_.write) stream_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!stream_) return DriverResult::internal("failed to seek file: " + path_);
  offset_ = offset;
  return DriverResult::success();
}

DriverResult LinuxHostFile::tell(uint64_t& offset) {
  if (!stream_.is_open()) return DriverResult::unavailable("file is not open");
  offset = offset_;
  return DriverResult::success();
}

DriverResult LinuxHostFile::flush() {
  if (!stream_.is_open()) return DriverResult::unavailable("file is not open");
  stream_.flush();
  if (!stream_) return DriverResult::internal("failed to flush file: " + path_);
  return DriverResult::success();
}

void LinuxHostFile::close() {
  if (stream_.is_open()) stream_.close();
  path_.clear();
  offset_ = 0;
  options_ = {};
}

bool LinuxHostFile::isOpen() const {
  return stream_.is_open();
}

uint64_t LinuxHostFile::size() const {
  if (path_.empty()) return 0;
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path_, ec);
  return ec ? 0 : static_cast<uint64_t>(file_size);
}

std::unique_ptr<IFile> LinuxHostFileSystemDriver::createFile() {
  return std::make_unique<LinuxHostFile>();
}

DriverResult LinuxHostFileSystemDriver::exists(const std::string& path, bool& result) {
  if (path.empty()) return DriverResult::invalidArgument("path is empty");
  std::error_code ec;
  result = std::filesystem::exists(path, ec);
  if (ec) return filesystemError("check path", path);
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::remove(const std::string& path) {
  if (path.empty()) return DriverResult::invalidArgument("path is empty");
  std::error_code ec;
  const auto removed = std::filesystem::remove_all(path, ec);
  if (ec) return filesystemError("remove path", path);
  if (removed == 0) return DriverResult::unavailable("path not found: " + path);
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::rename(const std::string& from, const std::string& to) {
  if (from.empty()) return DriverResult::invalidArgument("source path is empty");
  if (to.empty()) return DriverResult::invalidArgument("destination path is empty");
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  if (ec) return filesystemError("rename path", from + " -> " + to);
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::createDirectory(const std::string& path, bool recursive) {
  if (path.empty()) return DriverResult::invalidArgument("directory path is empty");
  std::error_code ec;
  if (recursive) {
    std::filesystem::create_directories(path, ec);
  } else {
    std::filesystem::create_directory(path, ec);
  }
  if (ec) return filesystemError("create directory", path);
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::listDirectory(const std::string& path, std::vector<FileInfo>& entries) {
  if (path.empty()) return DriverResult::invalidArgument("directory path is empty");
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec)) return DriverResult::unavailable("directory not found: " + path);
  if (ec) return filesystemError("check directory", path);

  entries.clear();
  for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
    if (ec) return filesystemError("list directory", path);
    FileInfo info;
    info.path = entry.path().lexically_normal().string();
    info.directory = entry.is_directory(ec);
    if (ec) return filesystemError("stat directory entry", info.path);
    if (!info.directory) {
      info.size = entry.file_size(ec);
      if (ec) info.size = 0;
    }
    entries.push_back(std::move(info));
  }
  return DriverResult::success();
}

DriverResult LinuxHostFileSystemDriver::stat(const std::string& path, FileInfo& info) {
  if (path.empty()) return DriverResult::invalidArgument("path is empty");
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return DriverResult::unavailable("path not found: " + path);
  if (ec) return filesystemError("stat path", path);

  const auto normalized = std::filesystem::path(path).lexically_normal().string();
  info.path = normalized;
  info.directory = std::filesystem::is_directory(path, ec);
  if (ec) return filesystemError("stat path", path);
  info.size = 0;
  if (!info.directory) {
    info.size = std::filesystem::file_size(path, ec);
    if (ec) return filesystemError("stat file size", path);
  }
  return DriverResult::success();
}

std::string LinuxHostFileSystemDriver::joinPath(const std::vector<std::string>& parts) {
  std::filesystem::path out;
  for (const auto& part : parts) {
    if (part.empty()) continue;
    if (out.empty()) {
      out = part;
    } else {
      out /= part;
    }
  }
  return out.lexically_normal().string();
}

std::string LinuxHostFileSystemDriver::normalizePath(const std::string& path) {
  if (path.empty()) return {};
  return std::filesystem::path(path).lexically_normal().string();
}

std::string LinuxHostFileSystemDriver::absolutePath(const std::string& path) {
  std::error_code ec;
  auto absolute_path = std::filesystem::absolute(path, ec);
  if (ec) return normalizePath(path);
  return absolute_path.lexically_normal().string();
}

DriverResult LinuxHostFileSystemDriver::filesystemError(const std::string& operation, const std::string& path) {
  return DriverResult::internal(operation + " failed: " + path);
}

} // namespace audio_studio::drivers::filesystem

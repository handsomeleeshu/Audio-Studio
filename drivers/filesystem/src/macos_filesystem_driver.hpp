#pragma once

#include <fstream>

#include "filesystem_driver.hpp"

namespace audio_studio::drivers::filesystem {

class MacOsFileSystemDriver;

class MacOsFile final : public IFile {
public:
  DriverResult open(const std::string& path, const FileOpenOptions& options) override;
  DriverResult read(void* buffer, size_t capacity, size_t& read_bytes) override;
  DriverResult write(const void* data, size_t size, size_t& written_bytes) override;
  DriverResult seek(uint64_t offset) override;
  DriverResult tell(uint64_t& offset) override;
  DriverResult flush() override;
  void close() override;
  bool isOpen() const override;
  uint64_t size() const override;

private:
  std::string path_;
  FileOpenOptions options_;
  uint64_t offset_ = 0;
  std::fstream stream_;
};

class MacOsFileSystemDriver final : public IFileSystemDriver {
public:
  std::unique_ptr<IFile> createFile() override;
  DriverResult exists(const std::string& path, bool& result) override;
  DriverResult remove(const std::string& path) override;
  DriverResult rename(const std::string& from, const std::string& to) override;
  DriverResult createDirectory(const std::string& path, bool recursive) override;
  DriverResult listDirectory(const std::string& path, std::vector<FileInfo>& entries) override;
  DriverResult stat(const std::string& path, FileInfo& info) override;
  std::string joinPath(const std::vector<std::string>& parts) override;
  std::string normalizePath(const std::string& path) override;
  std::string absolutePath(const std::string& path) override;

private:
  friend class MacOsFile;

  static DriverResult filesystemError(const std::string& operation, const std::string& path);
};

} // namespace audio_studio::drivers::filesystem
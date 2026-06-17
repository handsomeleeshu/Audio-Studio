#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "status.hpp"

namespace audio_studio::drivers::filesystem {

using DriverResult = framework::Status;

struct FileOpenOptions {
  bool read = true;
  bool write = false;
  bool create = false;
  bool truncate = false;
};

struct FileInfo {
  std::string path;
  uint64_t size = 0;
  bool directory = false;
};

class IFile {
public:
  virtual ~IFile() = default;

  virtual DriverResult open(const std::string& path, const FileOpenOptions& options) = 0;
  virtual DriverResult read(void* buffer, size_t capacity, size_t& read_bytes) = 0;
  virtual DriverResult write(const void* data, size_t size, size_t& written_bytes) = 0;
  virtual DriverResult seek(uint64_t offset) = 0;
  virtual DriverResult tell(uint64_t& offset) = 0;
  virtual DriverResult flush() = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual uint64_t size() const = 0;
};

class IFileSystemDriver {
public:
  virtual ~IFileSystemDriver() = default;

  virtual std::unique_ptr<IFile> createFile() = 0;
  virtual DriverResult exists(const std::string& path, bool& result) = 0;
  virtual DriverResult remove(const std::string& path) = 0;
  virtual DriverResult rename(const std::string& from, const std::string& to) = 0;
  virtual DriverResult createDirectory(const std::string& path, bool recursive) = 0;
  virtual DriverResult listDirectory(const std::string& path, std::vector<FileInfo>& entries) = 0;
  virtual DriverResult stat(const std::string& path, FileInfo& info) = 0;
  virtual std::string joinPath(const std::vector<std::string>& parts) = 0;
  virtual std::string normalizePath(const std::string& path) = 0;
  virtual std::string absolutePath(const std::string& path) = 0;
};

class IFileSystemDriverFactory {
public:
  virtual ~IFileSystemDriverFactory() = default;
  virtual std::string name() const = 0;
  virtual std::unique_ptr<IFileSystemDriver> create() const = 0;
};

class FileSystemDriverRegistry {
public:
  static FileSystemDriverRegistry& instance() {
    static FileSystemDriverRegistry registry;
    return registry;
  }

  DriverResult registerFactory(std::unique_ptr<IFileSystemDriverFactory> factory) {
    if (!factory) return DriverResult::invalidArgument("filesystem driver factory is null");
    const auto factory_name = factory->name();
    if (factory_name.empty()) return DriverResult::invalidArgument("filesystem driver factory name is empty");
    if (factories_.find(factory_name) != factories_.end()) {
      return DriverResult::invalidArgument("filesystem driver factory already registered: " + factory_name);
    }
    factories_.emplace(factory_name, std::move(factory));
    return DriverResult::success();
  }

  bool hasFactory(const std::string& name) const {
    return factories_.find(name) != factories_.end();
  }

  std::unique_ptr<IFileSystemDriver> create(const std::string& name) const {
    const auto it = factories_.find(name);
    if (it == factories_.end()) return nullptr;
    return it->second->create();
  }

  std::vector<std::string> factoryNames() const {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& item : factories_) names.push_back(item.first);
    return names;
  }

  void clear() {
    factories_.clear();
  }

private:
  std::map<std::string, std::unique_ptr<IFileSystemDriverFactory>> factories_;
};

} // namespace audio_studio::drivers::filesystem

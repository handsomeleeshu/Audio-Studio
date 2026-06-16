#pragma once

#include "dynlib_driver.hpp"

namespace audio_studio::drivers::dynlib {

class LinuxHostDynlibDriver;

class LinuxHostDynlib final : public IDynlib {
public:
  explicit LinuxHostDynlib(LinuxHostDynlibDriver& driver);
  ~LinuxHostDynlib() override;

  DriverResult open(const std::string& path, const DynlibOpenOptions& options) override;
  DriverResult getSymbol(const std::string& name, void** symbol) override;
  void close() override;
  bool isOpen() const override;
  std::string path() const override;

private:
  void* handle_ = nullptr;
  std::string path_;
};

class LinuxHostDynlibDriver final : public IDynlibDriver {
public:
  std::unique_ptr<IDynlib> createLibrary() override;
  std::string platformLibraryExtension() const override;
  bool isValidLibraryFile(const std::string& path) const override;
};

} // namespace audio_studio::drivers::dynlib

#pragma once

#include <map>

#include "dynlib_driver.hpp"

namespace audio_studio::drivers::dynlib {

class LinuxHostDynlibDriver;

class LinuxHostDynlib final : public IDynlib {
public:
  explicit LinuxHostDynlib(LinuxHostDynlibDriver& driver);

  DriverResult open(const std::string& path, const DynlibOpenOptions& options) override;
  DriverResult getSymbol(const std::string& name, void** symbol) override;
  void close() override;
  bool isOpen() const override;
  std::string path() const override;

private:
  LinuxHostDynlibDriver& driver_;
  std::string path_;
  bool open_ = false;
};

class LinuxHostDynlibDriver final : public IDynlibDriver {
public:
  std::unique_ptr<IDynlib> createLibrary() override;
  std::string platformLibraryExtension() const override;
  bool isValidLibraryFile(const std::string& path) const override;

  DriverResult registerTestSymbol(std::string name, void* symbol);
  DriverResult findTestSymbol(const std::string& name, void** symbol) const;

private:
  std::map<std::string, void*> symbols_;
};

class LinuxHostDynlibDriverFactory final : public IDynlibDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IDynlibDriver> create() const override {
    static int default_symbol = 42;
    auto driver = std::make_unique<LinuxHostDynlibDriver>();
    driver->registerTestSymbol("studio_create_plugin", &default_symbol);
    return driver;
  }
};

} // namespace audio_studio::drivers::dynlib

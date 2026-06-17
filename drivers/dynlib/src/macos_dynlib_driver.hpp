#pragma once

#include "dynlib_driver.hpp"

namespace audio_studio::drivers::dynlib {

class MacOsDynlibDriver;

class MacOsDynlib final : public IDynlib {
public:
  explicit MacOsDynlib(MacOsDynlibDriver& driver);
  ~MacOsDynlib() override;

  DriverResult open(const std::string& path, const DynlibOpenOptions& options) override;
  DriverResult getSymbol(const std::string& name, void** symbol) override;
  void close() override;
  bool isOpen() const override;
  std::string path() const override;

private:
  MacOsDynlibDriver& driver_;
  std::string path_;
  void* handle_ = nullptr;
};

class MacOsDynlibDriver final : public IDynlibDriver {
public:
  std::unique_ptr<IDynlib> createLibrary() override;
  std::string platformLibraryExtension() const override;
  bool isValidLibraryFile(const std::string& path) const override;

private:
  friend class MacOsDynlib;
};

} // namespace audio_studio::drivers::dynlib
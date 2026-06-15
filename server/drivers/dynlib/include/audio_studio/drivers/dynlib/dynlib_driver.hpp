#pragma once

#include <map>
#include <string>

#include "audio_studio/framework/status.hpp"

namespace audio_studio::drivers::dynlib {

class DynlibDriver {
public:
  framework::Status open(std::string path);
  framework::Status registerSymbol(std::string name, void* symbol);
  framework::Status getSymbol(const std::string& name, void*& out) const;
  void close();
  bool isOpen() const;
  const std::string& path() const;
  std::string platformLibraryExtension() const;
  bool isValidLibraryFile(const std::string& path) const;

private:
  std::string path_;
  bool open_ = false;
  std::map<std::string, void*> symbols_;
};

} // namespace audio_studio::drivers::dynlib

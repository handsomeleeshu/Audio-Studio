#include "audio_studio/drivers/dynlib/dynlib_driver.hpp"

#include <utility>

namespace audio_studio::drivers::dynlib {

framework::Status DynlibDriver::open(std::string path) {
  if (path.empty()) return framework::Status::invalidArgument("dynamic library path is empty");
  if (!isValidLibraryFile(path)) return framework::Status::invalidArgument("invalid dynamic library extension: " + path);
  path_ = std::move(path);
  open_ = true;
  return framework::Status::success();
}

framework::Status DynlibDriver::registerSymbol(std::string name, void* symbol) {
  if (!open_) return framework::Status::unavailable("dynamic library is not open");
  if (name.empty()) return framework::Status::invalidArgument("symbol name is empty");
  if (symbol == nullptr) return framework::Status::invalidArgument("symbol pointer is null");
  symbols_[std::move(name)] = symbol;
  return framework::Status::success();
}

framework::Status DynlibDriver::getSymbol(const std::string& name, void*& out) const {
  if (!open_) return framework::Status::unavailable("dynamic library is not open");
  const auto it = symbols_.find(name);
  if (it == symbols_.end()) return framework::Status::unavailable("symbol not found: " + name);
  out = it->second;
  return framework::Status::success();
}

void DynlibDriver::close() {
  open_ = false;
  path_.clear();
  symbols_.clear();
}

bool DynlibDriver::isOpen() const {
  return open_;
}

const std::string& DynlibDriver::path() const {
  return path_;
}

std::string DynlibDriver::platformLibraryExtension() const {
  return ".mock";
}

bool DynlibDriver::isValidLibraryFile(const std::string& path) const {
  const auto extension = platformLibraryExtension();
  return path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
}

} // namespace audio_studio::drivers::dynlib

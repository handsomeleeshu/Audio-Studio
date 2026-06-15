#include "linux_host_dynlib_driver.hpp"

#include <utility>

namespace audio_studio::drivers::dynlib {

LinuxHostDynlib::LinuxHostDynlib(LinuxHostDynlibDriver& driver) : driver_(driver) {}

DriverResult LinuxHostDynlib::open(const std::string& path, const DynlibOpenOptions& /*options*/) {
  if (path.empty()) return DriverResult::invalidArgument("dynamic library path is empty");
  if (!driver_.isValidLibraryFile(path)) return DriverResult::invalidArgument("invalid dynamic library extension: " + path);
  path_ = path;
  open_ = true;
  return DriverResult::success();
}

DriverResult LinuxHostDynlib::getSymbol(const std::string& name, void** symbol) {
  if (!open_) return DriverResult::unavailable("dynamic library is not open");
  return driver_.findTestSymbol(name, symbol);
}

void LinuxHostDynlib::close() {
  open_ = false;
  path_.clear();
}

bool LinuxHostDynlib::isOpen() const {
  return open_;
}

std::string LinuxHostDynlib::path() const {
  return path_;
}

std::unique_ptr<IDynlib> LinuxHostDynlibDriver::createLibrary() {
  return std::make_unique<LinuxHostDynlib>(*this);
}

std::string LinuxHostDynlibDriver::platformLibraryExtension() const {
  return ".mock";
}

bool LinuxHostDynlibDriver::isValidLibraryFile(const std::string& path) const {
  const auto extension = platformLibraryExtension();
  return path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
}

DriverResult LinuxHostDynlibDriver::registerTestSymbol(std::string name, void* symbol) {
  if (name.empty()) return DriverResult::invalidArgument("symbol name is empty");
  if (symbol == nullptr) return DriverResult::invalidArgument("symbol pointer is null");
  symbols_[std::move(name)] = symbol;
  return DriverResult::success();
}

DriverResult LinuxHostDynlibDriver::findTestSymbol(const std::string& name, void** symbol) const {
  if (symbol == nullptr) return DriverResult::invalidArgument("symbol output is null");
  const auto it = symbols_.find(name);
  if (it == symbols_.end()) return DriverResult::unavailable("symbol not found: " + name);
  *symbol = it->second;
  return DriverResult::success();
}

} // namespace audio_studio::drivers::dynlib

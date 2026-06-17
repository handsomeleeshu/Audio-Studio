#include "macos_dynlib_driver.hpp"

#include <dlfcn.h>

namespace audio_studio::drivers::dynlib {

namespace {

class MacOsDynlibDriverFactory final : public IDynlibDriverFactory {
public:
  std::string name() const override { return "macos"; }
  std::unique_ptr<IDynlibDriver> create() const override { return std::make_unique<MacOsDynlibDriver>(); }
};

const bool kMacOsDynlibDriverRegistered = [] {
  auto status = DynlibDriverRegistry::instance().registerFactory(std::make_unique<MacOsDynlibDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

MacOsDynlib::MacOsDynlib(MacOsDynlibDriver& driver) : driver_(driver) {}

MacOsDynlib::~MacOsDynlib() {
  close();
}

DriverResult MacOsDynlib::open(const std::string& path, const DynlibOpenOptions& options) {
  if (path.empty()) return DriverResult::invalidArgument("dynamic library path is empty");
  if (handle_ != nullptr) return DriverResult::invalidArgument("dynamic library is already open");

  // macOS uses RTLD_NOW by default for best performance
  // RTLD_LOCAL is the default on macOS; RTLD_GLOBAL allows symbols to be visible to other libraries
  int symbol_scope = RTLD_LOCAL;
  if (!options.local_symbols) {
    symbol_scope = RTLD_GLOBAL;
  }

  // On macOS, use RTLD_FIRST to avoid searching dependent libraries
  handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_FIRST | symbol_scope);
  if (handle_ == nullptr) {
    const char* error = ::dlerror();
    return DriverResult::unavailable("dlopen failed for " + path + ": " + (error == nullptr ? "unknown error" : error));
  }

  path_ = path;
  return DriverResult::success();
}

DriverResult MacOsDynlib::getSymbol(const std::string& name, void** symbol) {
  if (handle_ == nullptr) return DriverResult::unavailable("dynamic library is not open");
  if (symbol == nullptr) return DriverResult::invalidArgument("symbol output is null");
  if (name.empty()) return DriverResult::invalidArgument("symbol name is empty");

  ::dlerror();
  void* resolved = ::dlsym(handle_, name.c_str());
  const char* error = ::dlerror();
  if (error != nullptr) return DriverResult::unavailable("dlsym failed for " + name + ": " + error);
  *symbol = resolved;
  return DriverResult::success();
}

void MacOsDynlib::close() {
  if (handle_ != nullptr) {
    ::dlclose(handle_);
    handle_ = nullptr;
  }
  path_.clear();
}

bool MacOsDynlib::isOpen() const {
  return handle_ != nullptr;
}

std::string MacOsDynlib::path() const {
  return path_;
}

std::unique_ptr<IDynlib> MacOsDynlibDriver::createLibrary() {
  return std::make_unique<MacOsDynlib>(*this);
}

std::string MacOsDynlibDriver::platformLibraryExtension() const {
  return ".dylib";
}

bool MacOsDynlibDriver::isValidLibraryFile(const std::string& path) const {
  const auto extension = platformLibraryExtension();
  return path.size() >= extension.size() &&
         (path.compare(path.size() - extension.size(), extension.size(), extension) == 0 ||
          path.find(extension + ".") != std::string::npos);
}

} // namespace audio_studio::drivers::dynlib
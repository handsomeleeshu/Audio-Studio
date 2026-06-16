#include "linux_host_dynlib_driver.hpp"

#include <dlfcn.h>

namespace audio_studio::drivers::dynlib {

namespace {

class LinuxHostDynlibDriverFactory final : public IDynlibDriverFactory {
public:
  std::string name() const override { return "linux-host"; }
  std::unique_ptr<IDynlibDriver> create() const override { return std::make_unique<LinuxHostDynlibDriver>(); }
};

const bool kLinuxHostDynlibDriverRegistered = [] {
  auto status = DynlibDriverRegistry::instance().registerFactory(std::make_unique<LinuxHostDynlibDriverFactory>());
  (void)status;
  return true;
}();

} // namespace

LinuxHostDynlib::LinuxHostDynlib(LinuxHostDynlibDriver& /*driver*/) {}

LinuxHostDynlib::~LinuxHostDynlib() {
  close();
}

DriverResult LinuxHostDynlib::open(const std::string& path, const DynlibOpenOptions& options) {
  if (path.empty()) return DriverResult::invalidArgument("dynamic library path is empty");
  if (handle_ != nullptr) return DriverResult::invalidArgument("dynamic library is already open");

  const int symbol_scope = options.local_symbols ? RTLD_LOCAL : RTLD_GLOBAL;
  handle_ = ::dlopen(path.c_str(), RTLD_NOW | symbol_scope);
  if (handle_ == nullptr) {
    const char* error = ::dlerror();
    return DriverResult::unavailable("dlopen failed for " + path + ": " + (error == nullptr ? "unknown error" : error));
  }

  path_ = path;
  return DriverResult::success();
}

DriverResult LinuxHostDynlib::getSymbol(const std::string& name, void** symbol) {
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

void LinuxHostDynlib::close() {
  if (handle_ != nullptr) {
    ::dlclose(handle_);
    handle_ = nullptr;
  }
  path_.clear();
}

bool LinuxHostDynlib::isOpen() const {
  return handle_ != nullptr;
}

std::string LinuxHostDynlib::path() const {
  return path_;
}

std::unique_ptr<IDynlib> LinuxHostDynlibDriver::createLibrary() {
  return std::make_unique<LinuxHostDynlib>(*this);
}

std::string LinuxHostDynlibDriver::platformLibraryExtension() const {
  return ".so";
}

bool LinuxHostDynlibDriver::isValidLibraryFile(const std::string& path) const {
  const auto extension = platformLibraryExtension();
  return path.size() >= extension.size() &&
         (path.compare(path.size() - extension.size(), extension.size(), extension) == 0 ||
          path.find(extension + ".") != std::string::npos);
}

} // namespace audio_studio::drivers::dynlib

#pragma once

#include "os_driver.hpp"

namespace audio_studio::drivers::os {

class MacOsOsDriver final : public IOsDriver, public IOsClock, public IOsProcess, public IOsSystem {
public:
  std::unique_ptr<IOsThread> createThread() override;
  std::unique_ptr<IOsMutex> createMutex() override;
  std::unique_ptr<IOsRecursiveMutex> createRecursiveMutex() override;
  std::unique_ptr<IOsEvent> createEvent() override;
  std::unique_ptr<IOsSemaphore> createSemaphore(uint32_t initial_count, uint32_t max_count) override;
  std::unique_ptr<IOsTimer> createTimer() override;

  IOsClock& clock() override;
  IOsProcess& process() override;
  IOsSystem& system() override;

  uint64_t nowMs() const override;
  OsResult sleepForMs(uint64_t duration_ms) override;
  OsResult setEnv(std::string key, std::string value) override;
  OsResult getEnv(const std::string& key, std::string& out) const override;
  uint64_t processId() const override;
  std::string executablePath() const override;
  OsResult runCommand(const std::string& command, int& exit_code) override;
  OsResult getSystemInfo(OsSystemInfo& out) const override;
  std::string temporaryDirectory() const override;
  std::string homeDirectory() const override;

private:
};

} // namespace audio_studio::drivers::os

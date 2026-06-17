#pragma once

#include <string>

namespace audio_studio::framework {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kUnavailable,
  kInternal,
};

class Status {
public:
  Status();
  Status(StatusCode code, std::string message);

  static Status success();
  static Status invalidArgument(std::string message);
  static Status unavailable(std::string message);
  static Status internal(std::string message);

  bool ok() const;
  StatusCode code() const;
  const std::string& message() const;
  std::string codeString() const;
  std::string toJson() const;

private:
  StatusCode code_;
  std::string message_;
};

} // namespace audio_studio::framework

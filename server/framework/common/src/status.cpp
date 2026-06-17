#include "status.hpp"

#include <sstream>
#include <utility>

namespace audio_studio::framework {

Status::Status() : code_(StatusCode::kOk), message_("ok") {}

Status::Status(StatusCode code, std::string message)
  : code_(code), message_(std::move(message)) {}

Status Status::success() {
  return {};
}

Status Status::invalidArgument(std::string message) {
  return {StatusCode::kInvalidArgument, std::move(message)};
}

Status Status::unavailable(std::string message) {
  return {StatusCode::kUnavailable, std::move(message)};
}

Status Status::internal(std::string message) {
  return {StatusCode::kInternal, std::move(message)};
}

bool Status::ok() const {
  return code_ == StatusCode::kOk;
}

StatusCode Status::code() const {
  return code_;
}

const std::string& Status::message() const {
  return message_;
}

std::string Status::codeString() const {
  switch (code_) {
    case StatusCode::kOk: return "OK";
    case StatusCode::kInvalidArgument: return "INVALID_ARGUMENT";
    case StatusCode::kUnavailable: return "UNAVAILABLE";
    case StatusCode::kInternal: return "INTERNAL";
  }
  return "UNKNOWN";
}

std::string Status::toJson() const {
  std::ostringstream out;
  out << "{\"ok\":" << (ok() ? "true" : "false")
      << ",\"code\":\"" << codeString()
      << "\",\"message\":\"" << message_ << "\"}";
  return out.str();
}

} // namespace audio_studio::framework

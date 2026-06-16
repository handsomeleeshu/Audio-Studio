#include "audio_studio/rpc/rpc_framing.hpp"

#include <cctype>
#include <cstdint>
#include <sstream>

namespace audio_studio::rpc {
namespace {

constexpr const char* kHeaderEnd = "\r\n\r\n";

size_t parseContentLength(const std::string& header) {
  const std::string key = "Content-Length:";
  const auto pos = header.find(key);
  if (pos == std::string::npos) throw RpcFrameError("missing Content-Length header");
  size_t begin = pos + key.size();
  while (begin < header.size() && std::isspace(static_cast<unsigned char>(header[begin]))) ++begin;
  size_t end = begin;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end]))) ++end;
  if (begin == end) throw RpcFrameError("invalid Content-Length header");
  return static_cast<size_t>(std::stoull(header.substr(begin, end - begin)));
}

} // namespace

RpcFrameError::RpcFrameError(const std::string& message) : std::runtime_error(message) {}

std::string encodeContentLengthFrame(const std::string& json) {
  std::ostringstream out;
  out << "Content-Length: " << json.size() << "\r\n\r\n" << json;
  return out.str();
}

std::string readContentLengthFrame(const RpcReadByte& read_byte, size_t max_payload_size) {
  std::string header;
  header.reserve(64);
  while (header.find(kHeaderEnd) == std::string::npos) {
    header.push_back(read_byte());
    if (header.size() > 4096) throw RpcFrameError("RPC frame header is too large");
  }

  const auto header_end = header.find(kHeaderEnd);
  const auto payload_prefix = header.substr(header_end + 4);
  header.resize(header_end + 4);

  const size_t content_length = parseContentLength(header);
  if (content_length > max_payload_size) throw RpcFrameError("RPC frame payload is too large");

  std::string payload = payload_prefix;
  payload.reserve(content_length);
  while (payload.size() < content_length) payload.push_back(read_byte());
  if (payload.size() > content_length) payload.resize(content_length);
  return payload;
}

void writeContentLengthFrame(const RpcWriteBytes& write_bytes, const std::string& json) {
  const std::string frame = encodeContentLengthFrame(json);
  write_bytes(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

} // namespace audio_studio::rpc

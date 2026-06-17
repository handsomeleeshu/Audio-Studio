#pragma once

#include "rpc_framing.hpp"

namespace audio_studio::rpc {

class IRpcStreamTransport {
public:
  virtual ~IRpcStreamTransport() = default;
  virtual void open() = 0;
  virtual RpcBinaryFrame exchange(const RpcBinaryFrame& frame) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
};

} // namespace audio_studio::rpc

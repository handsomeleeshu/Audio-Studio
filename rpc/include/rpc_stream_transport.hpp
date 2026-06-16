#pragma once

#include "rpc_framing.hpp"

namespace audio_studio::rpc {

class IRpcStreamTransport {
public:
  virtual ~IRpcStreamTransport() = default;
  virtual RpcBinaryFrame exchange(const RpcBinaryFrame& frame) = 0;
};

} // namespace audio_studio::rpc

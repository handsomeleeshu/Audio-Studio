#include "datalink_manager.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace audio_studio::framework::transport {

DataLinkManager::DataLinkManager(drivers::datalink::IDataLinkDevice& device, DataLinkManagerConfig config)
  : device_(device), config_(config) {}

framework::Status DataLinkManager::sendPacket(const std::vector<uint8_t>& payload, uint32_t timeout_ms) {
  if (!device_.isConnected()) return framework::Status::unavailable("data-link device is not connected");

  const auto caps = device_.caps();
  if (caps.max_payload_size <= DataLinkFrameCodec::kHeaderSize) {
    return framework::Status::invalidArgument("data-link MTU is too small");
  }
  const size_t fragment_capacity = caps.max_payload_size - DataLinkFrameCodec::kHeaderSize;
  const size_t fragment_count = std::max<size_t>(1, (payload.size() + fragment_capacity - 1) / fragment_capacity);
  if (fragment_count > 0xffffu) return framework::Status::invalidArgument("data-link packet has too many fragments");

  const uint32_t link_sequence = next_link_sequence_++;
  for (size_t index = 0; index < fragment_count; ++index) {
    const size_t offset = index * fragment_capacity;
    const size_t count = std::min(fragment_capacity, payload.size() - offset);

    DataLinkFrame frame;
    frame.flags = kDataLinkFrameData;
    if (index + 1 == fragment_count) frame.flags |= kDataLinkFrameEnd;
    frame.link_sequence = link_sequence;
    frame.transport_size = static_cast<uint32_t>(payload.size());
    frame.fragment_offset = static_cast<uint32_t>(offset);
    frame.fragment_index = static_cast<uint16_t>(index);
    frame.fragment_count = static_cast<uint16_t>(fragment_count);
    frame.payload.assign(payload.begin() + static_cast<long>(offset),
                         payload.begin() + static_cast<long>(offset + count));
    const auto encoded = DataLinkFrameCodec::encode(frame);

    bool delivered = false;
    for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
      auto status = device_.writeBlock(encoded.data(), encoded.size(), timeout_ms);
      if (!status.ok()) return status;
      ++stats_.fragments_sent;

      DataLinkFrame ack;
      status = readFrame(ack, config_.ack_timeout_ms);
      const bool ack_matches = status.ok() &&
                               ack.link_sequence == frame.link_sequence &&
                               ack.fragment_index == frame.fragment_index &&
                               ((ack.flags & kDataLinkFrameAck) != 0);
      if (ack_matches) {
        delivered = true;
        break;
      }
      if (attempt == config_.max_retries) {
        return status.ok() ? framework::Status::unavailable("data-link fragment was not acknowledged")
                           : status;
      }
      ++stats_.retries;
    }
    if (!delivered) return framework::Status::unavailable("data-link fragment delivery failed");
  }

  ++stats_.packets_sent;
  return framework::Status::success();
}

framework::Status DataLinkManager::receivePacket(std::vector<uint8_t>& payload, uint32_t timeout_ms) {
  payload.clear();
  DataLinkFrame first;
  auto status = readFrame(first, timeout_ms);
  if (!status.ok()) return status;
  if ((first.flags & kDataLinkFrameData) == 0) return framework::Status::invalidArgument("data-link expected DATA frame");
  if (first.fragment_offset != 0) return framework::Status::invalidArgument("data-link first fragment offset is not zero");

  payload.assign(first.transport_size, 0);
  auto copy_fragment = [&](const DataLinkFrame& frame) -> framework::Status {
    if (frame.transport_size != first.transport_size) return framework::Status::invalidArgument("data-link transport size changed");
    if (frame.fragment_offset + frame.payload.size() > payload.size()) {
      return framework::Status::invalidArgument("data-link fragment exceeds packet size");
    }
    std::copy(frame.payload.begin(), frame.payload.end(), payload.begin() + static_cast<long>(frame.fragment_offset));
    ++stats_.fragments_received;
    return sendAck(frame, true, timeout_ms);
  };

  status = copy_fragment(first);
  if (!status.ok()) return status;
  for (uint16_t expected = 1; expected < first.fragment_count; ++expected) {
    DataLinkFrame frame;
    status = readFrame(frame, timeout_ms);
    if (!status.ok()) return status;
    if ((frame.flags & kDataLinkFrameData) == 0 || frame.link_sequence != first.link_sequence || frame.fragment_index != expected) {
      (void)sendAck(frame, false, timeout_ms);
      return framework::Status::invalidArgument("data-link fragment sequence mismatch");
    }
    status = copy_fragment(frame);
    if (!status.ok()) return status;
  }

  ++stats_.packets_received;
  return framework::Status::success();
}

const DataLinkManagerStats& DataLinkManager::stats() const {
  return stats_;
}

framework::Status DataLinkManager::readFrame(DataLinkFrame& frame, uint32_t timeout_ms) {
  const auto caps = device_.caps();
  const size_t read_capacity = std::max<size_t>(caps.max_payload_size, DataLinkFrameCodec::kHeaderSize);
  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::milliseconds(timeout_ms);
  while (true) {
    size_t complete_size = 0;
    auto status = DataLinkFrameCodec::frameSize(rx_buffer_.data(), rx_buffer_.size(), complete_size);
    if (status.ok()) {
      status = DataLinkFrameCodec::decode(rx_buffer_.data(), complete_size, frame);
      rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<long>(complete_size));
      return status;
    }
    if (status.code() != framework::StatusCode::kUnavailable) return status;

    std::vector<uint8_t> buffer(read_capacity);
    size_t actual = 0;
    uint32_t remaining_ms = timeout_ms;
    if (timeout_ms > 0) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) return framework::Status::unavailable("data-link read timed out");
      remaining_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
      if (remaining_ms == 0) remaining_ms = 1;
    }
    status = device_.readBlock(buffer.data(), buffer.size(), actual, remaining_ms);
    if (!status.ok()) return status;
    if (actual == 0) {
      if (timeout_ms == 0) return framework::Status::unavailable("data-link read returned no data");
      if (std::chrono::steady_clock::now() >= deadline) {
        return framework::Status::unavailable("data-link read timed out");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    rx_buffer_.insert(rx_buffer_.end(), buffer.begin(), buffer.begin() + static_cast<long>(actual));
  }
}

framework::Status DataLinkManager::sendAck(const DataLinkFrame& frame, bool ok, uint32_t timeout_ms) {
  const auto ack = ok ? DataLinkFrameCodec::makeAck(frame) : DataLinkFrameCodec::makeNak(frame);
  const auto encoded = DataLinkFrameCodec::encode(ack);
  return device_.writeBlock(encoded.data(), encoded.size(), timeout_ms);
}

} // namespace audio_studio::framework::transport

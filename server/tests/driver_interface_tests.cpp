#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "driver_manager.hpp"

#ifndef AUDIO_STUDIO_TEST_PLUGIN_PATH
#error "AUDIO_STUDIO_TEST_PLUGIN_PATH is required"
#endif

namespace {

bool runHostAudioBackendTests() {
  const char* value = std::getenv("AUDIO_STUDIO_RUN_HOST_AUDIO_BACKEND_TESTS");
  return value != nullptr && std::string(value) == "1";
}

#if defined(__APPLE__)
  constexpr const char* kOsDriverName = "macos";
  constexpr const char* kSocketDriverName = "macos";
  constexpr const char* kFilesystemDriverName = "macos";
  constexpr const char* kPipeDriverName = "macos";
  constexpr const char* kDynlibDriverName = "macos";
  constexpr const char* kDataLinkDeviceName = "macos";
  constexpr const char* kControlDriverName = "macos";
  constexpr const char* kLogDriverName = "macos";
  constexpr const char* kDumpDriverName = "macos";
  constexpr const char* kAudioDriverName = "macos";
  constexpr const char* kDynlibExt = ".dylib";
  constexpr const char* kPlatformName = "Darwin";
#else
  constexpr const char* kOsDriverName = "linux-host";
  constexpr const char* kSocketDriverName = "linux-host";
  constexpr const char* kFilesystemDriverName = "linux-host";
  constexpr const char* kPipeDriverName = "linux-host";
  constexpr const char* kDynlibDriverName = "linux-host";
  constexpr const char* kDataLinkDeviceName = "linux-host";
  constexpr const char* kControlDriverName = "linux-host";
  constexpr const char* kLogDriverName = "linux-host";
  constexpr const char* kDumpDriverName = "linux-host";
  constexpr const char* kAudioDriverName = "alsa";
  constexpr const char* kDynlibExt = ".so";
  constexpr const char* kPlatformName = "Linux";
#endif

} // namespace

int main() {
  audio_studio::drivers::DriverManager manager;
  assert(manager.initialize().ok());
  assert(manager.initialized());
  assert(manager.hasDriver("os", kOsDriverName));
  assert(manager.hasDriver("socket", kSocketDriverName));
  assert(manager.hasDriver("audio", kAudioDriverName));
  assert(manager.listByCategory("dump").size() == 1);

  {
    audio_studio::drivers::DriverInfo info;
    assert(manager.getDriver("socket", kSocketDriverName, info).ok());
    assert(info.active);
  }

  {
    auto& os = manager.os();
    auto thread = os.createThread();
    bool thread_ran = false;
    assert(thread->start("driver-test-thread", [&] { thread_ran = true; }).ok());
    assert(thread->join().ok());
    assert(thread_ran);

    auto mutex = os.createMutex();
    mutex->lock();
    mutex->unlock();
    assert(mutex->tryLock());
    mutex->unlock();

    auto recursive_mutex = os.createRecursiveMutex();
    recursive_mutex->lock();
    recursive_mutex->lock();
    recursive_mutex->unlock();
    recursive_mutex->unlock();

    auto event = os.createEvent();
    assert(!event->wait(1).ok());
    event->signal();
    assert(event->wait(1).ok());
    assert(event->isSignaled());
    event->reset();
    assert(!event->isSignaled());

    auto semaphore = os.createSemaphore(1, 2);
    assert(semaphore->count() == 1);
    assert(semaphore->acquire(1).ok());
    assert(semaphore->count() == 0);
    assert(!semaphore->acquire(1).ok());
    assert(semaphore->release(2).ok());
    assert(semaphore->count() == 2);

    auto timer = os.createTimer();
    assert(timer->startOnce(0).ok());
    for (int i = 0; i < 50 && !timer->expired(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(timer->expired());
    assert(!timer->active());
    timer->stop();
    assert(!timer->active());

    auto& clock = os.clock();
    const auto before_sleep = clock.nowMs();
    assert(clock.sleepForMs(1).ok());
    assert(clock.nowMs() >= before_sleep);

    auto& process = os.process();
    assert(process.setEnv("AS_TEST", "1").ok());
    std::string env;
    assert(process.getEnv("AS_TEST", env).ok());
    assert(env == "1");
    assert(process.processId() == static_cast<uint64_t>(::getpid()));

    auto& system = os.system();
    audio_studio::drivers::os::OsSystemInfo system_info;
    assert(system.getSystemInfo(system_info).ok());
    assert(system_info.platform.find(kPlatformName) != std::string::npos);
    assert(system_info.pid == static_cast<uint64_t>(::getpid()));
    assert(!system.temporaryDirectory().empty());
    assert(!system.homeDirectory().empty());
  }

  {
    auto& socket_driver = manager.socket();
    auto server = socket_driver.createSocket(audio_studio::drivers::socket::SocketType::Tcp);
    assert(server);
    assert(server->open({audio_studio::drivers::socket::SocketType::Tcp}).ok());

    bool bound = false;
    uint16_t port = static_cast<uint16_t>(24000 + (::getpid() % 20000));
    for (uint16_t attempt = 0; attempt < 100; ++attempt) {
      const uint16_t candidate = static_cast<uint16_t>(port + attempt);
      auto status = server->bind({"127.0.0.1", candidate});
      if (status.ok()) {
        port = candidate;
        bound = true;
        break;
      }
    }
    assert(bound);
    assert(server->listen(1).ok());

    audio_studio::framework::Status accept_status;
    audio_studio::framework::Status server_recv_status;
    audio_studio::framework::Status server_send_status;
    std::thread server_thread([&] {
      std::unique_ptr<audio_studio::drivers::socket::ISocket> accepted;
      accept_status = server->accept(accepted, 5000);
      if (!accept_status.ok()) return;
      uint8_t rx[3] = {};
      size_t received = 0;
      server_recv_status = accepted->recv(rx, sizeof(rx), received, 5000);
      if (!server_recv_status.ok()) return;
      assert(received == sizeof(rx));
      assert(rx[0] == 1 && rx[2] == 3);
      const uint8_t tx[] = {3, 2, 1};
      size_t sent = 0;
      server_send_status = accepted->send(tx, sizeof(tx), sent, 5000);
      assert(sent == sizeof(tx));
      accepted->close();
    });

    auto client = socket_driver.createSocket(audio_studio::drivers::socket::SocketType::Tcp);
    assert(client);
    assert(client->open({audio_studio::drivers::socket::SocketType::Tcp}).ok());
    assert(client->connect({"127.0.0.1", port}, 5000).ok());
    const uint8_t tx[] = {1, 2, 3};
    size_t sent = 0;
    assert(client->send(tx, sizeof(tx), sent, 5000).ok());
    assert(sent == sizeof(tx));
    uint8_t rx[3] = {};
    size_t received = 0;
    assert(client->recv(rx, sizeof(rx), received, 5000).ok());
    assert(received == sizeof(rx));
    assert(rx[0] == 3 && rx[2] == 1);
    client->close();

    server_thread.join();
    assert(accept_status.ok());
    assert(server_recv_status.ok());
    assert(server_send_status.ok());
    server->close();
  }

  {
    auto& fs = manager.filesystem();
    const auto root = fs.joinPath({fs.absolutePath(fs.normalizePath(std::filesystem::temp_directory_path().string())),
                                  "audio-studio-driver-test-" + std::to_string(::getpid())});
    (void)fs.remove(root);
    assert(fs.createDirectory(root, true).ok());
    auto file = fs.createFile();
    const auto path = fs.joinPath({root, "audio-studio.txt"});
    audio_studio::drivers::filesystem::FileOpenOptions options;
    options.read = true;
    options.write = true;
    options.create = true;
    options.truncate = true;
    assert(file->open(path, options).ok());
    const char text[] = "content";
    size_t written = 0;
    assert(file->write(text, std::strlen(text), written).ok());
    assert(written == std::strlen(text));
    assert(file->seek(0).ok());
    char buffer[16] = {};
    size_t read = 0;
    assert(file->read(buffer, sizeof(buffer), read).ok());
    assert(std::string(buffer, read) == "content");
    audio_studio::drivers::filesystem::FileInfo info;
    assert(fs.stat(path, info).ok());
    assert(info.size == std::strlen(text));
    std::vector<audio_studio::drivers::filesystem::FileInfo> entries;
    assert(fs.listDirectory(root, entries).ok());
    assert(entries.size() == 1);
    file->close();
    assert(fs.remove(root).ok());
  }

  {
    auto& pipe_driver = manager.pipe();
    audio_studio::drivers::pipe::PipeEndpoint endpoint{
      std::filesystem::temp_directory_path().string() + "/as-" + std::to_string(::getpid()) + ".pipe"};
    bool stale_exists = false;
    assert(pipe_driver.exists(endpoint, stale_exists).ok());
    if (stale_exists) assert(pipe_driver.removePipe(endpoint, audio_studio::drivers::pipe::PipeType::Fifo).ok());
    assert(pipe_driver.createPipe(endpoint, audio_studio::drivers::pipe::PipeType::Fifo).ok());
    bool exists = false;
    assert(pipe_driver.exists(endpoint, exists).ok());
    assert(exists);
    auto stream = pipe_driver.createPipeStream(audio_studio::drivers::pipe::PipeType::Fifo);
    assert(stream->open({endpoint, audio_studio::drivers::pipe::PipeType::Fifo}).ok());
    const uint8_t tx[] = {9, 8, 7};
    size_t written = 0;
    assert(stream->write(tx, sizeof(tx), written, 100).ok());
    uint8_t rx[3] = {};
    size_t read = 0;
    assert(stream->read(rx, sizeof(rx), read, 100).ok());
    assert(read == sizeof(rx));
    assert(rx[0] == 9 && rx[2] == 7);
    stream->close();
    assert(pipe_driver.removePipe(endpoint, audio_studio::drivers::pipe::PipeType::Fifo).ok());
  }

  {
    auto& dynlib_driver = manager.dynlib();
    auto lib = dynlib_driver.createLibrary();
    assert(dynlib_driver.platformLibraryExtension() == kDynlibExt);
    assert(dynlib_driver.isValidLibraryFile(AUDIO_STUDIO_TEST_PLUGIN_PATH));
    assert(lib->open(AUDIO_STUDIO_TEST_PLUGIN_PATH, {}).ok());
    void* symbol = nullptr;
    assert(lib->getSymbol("studio_create_plugin", &symbol).ok());
    assert(symbol != nullptr);
    using CreatePluginFn = int (*)();
    auto create_plugin = reinterpret_cast<CreatePluginFn>(symbol);
    assert(create_plugin() == 7);
    lib->close();
  }

  {
    audio_studio::drivers::datalink::DataLinkDeviceConfig config;
    config.name = "memory";
    auto datalink = manager.datalinkRegistry().create(kDataLinkDeviceName, config);
    assert(datalink);
    const uint8_t tx[] = {4, 5, 6, 7};
    assert(datalink->writeBlock(tx, sizeof(tx), 100).ok());
    uint8_t rx[4] = {};
    size_t actual = 0;
    assert(datalink->readBlock(rx, sizeof(rx), actual, 100).ok());
    assert(actual == sizeof(rx));
    assert(datalink->isConnected());
    assert(datalink->name() == "memory");
  }

  if (runHostAudioBackendTests()) {
    {
      std::unique_ptr<audio_studio::drivers::audio::IAudioPlaybackDevice> playback;
      assert(manager.audioRegistry().createPlayback(kAudioDriverName, {"default"}, playback).ok());
      assert(playback);
      assert(playback->prepare({48000, 2, 2}).ok());
      assert(playback->start().ok());
      assert(playback->writeFrame({0, 1, 2, 3}, 100).ok());
      assert(playback->getStats().frames_written == 1);
      assert(playback->drain().ok());
      assert(playback->stop().ok());

      std::unique_ptr<audio_studio::drivers::audio::IAudioCaptureDevice> capture;
      assert(manager.audioRegistry().createCapture(kAudioDriverName, {"default"}, capture).ok());
      assert(capture);
      assert(capture->prepare({48000, 1, 2}).ok());
      assert(capture->start().ok());
      audio_studio::drivers::audio::AudioFrame frame(2);
      assert(capture->readFrame(frame, 100).ok());
      assert(frame.size() == 2);
    }

#if !defined(__APPLE__)
    // ALSA and Pulse are Linux-only backends
    {
      std::unique_ptr<audio_studio::drivers::audio::IAudioPlaybackDevice> playback;
      assert(manager.audioRegistry().createPlayback("pulse", {"default"}, playback).ok());
      assert(playback);
      assert(playback->prepare({48000, 2, 2}).ok());
      assert(playback->start().ok());
      assert(playback->writeFrame(std::vector<uint8_t>(480, 0), 1000).ok());
      assert(playback->getStats().frames_written == 120);
      assert(playback->drain().ok());
      assert(playback->stop().ok());

      std::unique_ptr<audio_studio::drivers::audio::IAudioCaptureDevice> capture;
      assert(manager.audioRegistry().createCapture("pulse", {"default"}, capture).ok());
      assert(capture);
      assert(capture->prepare({48000, 2, 2}).ok());
      assert(capture->start().ok());
      audio_studio::drivers::audio::AudioFrame frame(480);
      assert(capture->readFrame(frame, 1000).ok());
      assert(frame.size() == 480);
      assert(capture->stop().ok());
    }
#endif
  }

  {
    auto control = manager.controlRegistry().create(kControlDriverName, {"a2", "host-control"});
    assert(control);
    audio_studio::drivers::control::ControlValue value;
    value.type = audio_studio::drivers::control::ControlValueType::String;
    value.text = "-6";
    assert(control->setValue("gain", value, 100).ok());
    audio_studio::drivers::control::ControlValue out;
    assert(control->getValue("gain", out, 100).ok());
    assert(out.text == "-6");
    std::vector<audio_studio::drivers::control::ControlInfo> controls;
    assert(control->listControls(controls).ok());
    assert(controls.size() == 1);
    audio_studio::drivers::control::ControlDeviceStats stats;
    assert(control->getStats(stats).ok());
    assert(stats.writes == 1);
  }

  {
    auto log = manager.logRegistry().create(kLogDriverName, {"firmware"});
    assert(log);
    assert(log->start().ok());
    audio_studio::drivers::log::LogRawChunk chunk;
    assert(log->readChunk(chunk, 100).ok());
    assert(chunk.sequence == 1);
    audio_studio::drivers::log::LogDeviceStats stats;
    assert(log->getStats(stats).ok());
    assert(stats.chunks_read == 1);
  }

  {
    auto dump = manager.dumpRegistry().create(kDumpDriverName, {"probe"});
    assert(dump);
    assert(dump->configure({"dump-session"}).ok());
    assert(dump->addPoint({7, "AEC.out"}).ok());
    std::vector<audio_studio::drivers::dump::DumpPointInfo> points;
    assert(dump->listPoints(points).ok());
    assert(points.size() == 1);
    assert(dump->start().ok());
    audio_studio::drivers::dump::DumpRawPacket packet;
    assert(dump->readPacket(packet, 100).ok());
    assert(packet.point_id == 7);
    audio_studio::drivers::dump::DumpDeviceStats stats;
    assert(dump->getStats(stats).ok());
    assert(stats.packets_read == 1);
  }

  manager.shutdown();
  assert(!manager.initialized());
  return 0;
}

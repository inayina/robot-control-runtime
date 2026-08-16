#include "rcr/workbench/services/cell_app_client.hpp"
#include "rcr/workbench/services/cell_app_server.hpp"

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using rcr::workbench::CellAppClient;
using rcr::workbench::CellAppHandler;
using rcr::workbench::CellAppServer;
using rcr::workbench::CellAppStatus;
using rcr::workbench::CommandReply;
using rcr::workbench::CommandStatus;
using rcr::workbench::DigitalOutputRequest;
using rcr::workbench::RuntimeModeCode;

class FakeHandler final : public CellAppHandler {
public:
  CellAppStatus status() override {
    CellAppStatus out;
    out.started = true;
    out.online = true;
    out.cell_ready = ready_;
    out.mode = RuntimeModeCode::Active;
    out.session_id = 12;
    out.node_id = 1;
    return out;
  }

  CommandReply activate() override {
    activated_ = true;
    return {CommandStatus::Accepted, RuntimeModeCode::Idle,
            RuntimeModeCode::Active, "activated"};
  }

  CommandReply submit_output(const DigitalOutputRequest &request) override {
    last_output_ = request;
    return {CommandStatus::Accepted, RuntimeModeCode::Active,
            RuntimeModeCode::Active, "submitted"};
  }

  bool activated() const { return activated_; }
  const DigitalOutputRequest &last_output() const { return last_output_; }

private:
  bool activated_{false};
  bool ready_{true};
  DigitalOutputRequest last_output_{};
};

RCR_TEST(localhost_get_status_and_activate) {
  FakeHandler handler;
  CellAppServer server{handler};
  RCR_REQUIRE(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  std::atomic<bool> run{true};
  std::thread worker([&] {
    while (run.load()) {
      static_cast<void>(server.poll(std::chrono::milliseconds{50}));
    }
  });
  struct Join {
    std::atomic<bool> *run;
    std::thread *worker;
    ~Join() {
      run->store(false);
      if (worker->joinable()) {
        worker->join();
      }
    }
  } guard{&run, &worker};
  CellAppClient client;
  const auto connected =
      client.connect("127.0.0.1", port, std::chrono::milliseconds{500});
  RCR_EXPECT(connected.ok());
  const auto status = client.get_status(std::chrono::milliseconds{1000});
  RCR_REQUIRE(status.ok());
  RCR_EXPECT(status.value().started);
  RCR_EXPECT(status.value().cell_ready);
  RCR_EXPECT(status.value().session_id == 12);
  const auto activated = client.activate(std::chrono::milliseconds{1000});
  RCR_REQUIRE(activated.ok());
  RCR_EXPECT(activated.value().accepted());
  RCR_EXPECT(handler.activated());
  DigitalOutputRequest request;
  request.session_id = 12;
  request.sequence = 3;
  request.valid_for_ms = 2000;
  request.mask = 1;
  request.values = 1;
  const auto submitted =
      client.submit_output(request, std::chrono::milliseconds{1000});
  RCR_REQUIRE(submitted.ok());
  RCR_EXPECT(submitted.value().accepted());
  RCR_EXPECT(handler.last_output().session_id == 12);
  RCR_EXPECT(handler.last_output().values == 1);
  client.disconnect();
}

RCR_TEST(timeout_disconnects_so_next_command_is_not_desynced) {
  FakeHandler handler;
  CellAppServer server{handler};
  RCR_REQUIRE(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  CellAppClient client;
  RCR_REQUIRE(
      client.connect("127.0.0.1", port, std::chrono::milliseconds{500}).ok());
  const auto timed_out = client.get_status(std::chrono::milliseconds{20});
  RCR_EXPECT(!timed_out.ok());
  RCR_EXPECT(!client.connected());
  RCR_REQUIRE(
      client.reconnect(std::chrono::milliseconds{500}).ok());
  std::atomic<bool> run{true};
  std::thread worker([&] {
    while (run.load()) {
      static_cast<void>(server.poll(std::chrono::milliseconds{50}));
    }
  });
  struct Join {
    std::atomic<bool> *run;
    std::thread *worker;
    ~Join() {
      run->store(false);
      if (worker->joinable()) {
        worker->join();
      }
    }
  } guard{&run, &worker};
  const auto status = client.get_status(std::chrono::milliseconds{1000});
  RCR_EXPECT(status.ok());
}

RCR_TEST(unconnected_client_does_not_invent_status) {
  CellAppClient client;
  const auto status = client.get_status(std::chrono::milliseconds{50});
  RCR_EXPECT(!status.ok());
}

} // namespace

RCR_TEST_MAIN()

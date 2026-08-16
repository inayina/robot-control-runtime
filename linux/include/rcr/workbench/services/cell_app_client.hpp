#pragma once

// ThinkPad 工程站客户端。只发 CEL1 观察/命令；不打开 SocketCAN，也不跑 CellReady 闭环。

#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/workbench/application/cell_app_protocol.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace rcr::workbench {

class CellAppClient {
public:
  ~CellAppClient();

  [[nodiscard]] Result<void> connect(const std::string &host, std::uint16_t port,
                                     std::chrono::milliseconds timeout);
  void disconnect() noexcept;
  [[nodiscard]] bool connected() const noexcept { return fd_.valid(); }

  [[nodiscard]] Result<CellAppStatus>
  get_status(std::chrono::milliseconds timeout);
  [[nodiscard]] Result<CommandReply>
  activate(std::chrono::milliseconds timeout);
  [[nodiscard]] Result<CommandReply>
  submit_output(const DigitalOutputRequest &request,
                std::chrono::milliseconds timeout);

private:
  [[nodiscard]] Result<CellAppFrame>
  exchange(CellAppMessage request_type, CellAppMessage ack_type,
           std::vector<std::uint8_t> payload,
           std::chrono::milliseconds timeout);

  OwnedFd fd_{};
  std::uint16_t next_sequence_{1};
};

} // namespace rcr::workbench

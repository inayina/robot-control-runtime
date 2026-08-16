#pragma once

// Orange Pi Cell 应用的工程站 TCP。只做 framing / accept / 分发；闭环 tick 留在
// rcr_cell_app，本对象不拥有 RuntimeDaemon 或 CellReadyMapper。

#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/workbench/application/cell_app_protocol.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace rcr::workbench {

class CellAppHandler {
public:
  virtual ~CellAppHandler() = default;
  [[nodiscard]] virtual CellAppStatus status() = 0;
  [[nodiscard]] virtual CommandReply activate() = 0;
  [[nodiscard]] virtual CommandReply
  submit_output(const DigitalOutputRequest &request) = 0;
};

class CellAppServer {
public:
  explicit CellAppServer(CellAppHandler &handler);
  ~CellAppServer();

  [[nodiscard]] Result<void> listen(const std::string &bind_address,
                                    std::uint16_t port);
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  void close() noexcept;

  // 有界 poll：accept 新工程站、处理已就绪请求后返回，避免堵住 CellReady 边沿。
  [[nodiscard]] Result<void> poll(std::chrono::milliseconds timeout);

private:
  struct ClientSession {
    OwnedFd fd{};
    std::vector<std::uint8_t> buffer{};
  };

  void drop_client(std::size_t index) noexcept;
  [[nodiscard]] Result<void> accept_pending();
  [[nodiscard]] Result<void> service_client(std::size_t index);

  CellAppHandler &handler_;
  OwnedFd listen_fd_{};
  std::vector<ClientSession> clients_{};
  std::uint16_t port_{0};
};

} // namespace rcr::workbench

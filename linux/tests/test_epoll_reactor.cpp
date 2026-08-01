// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/epoll_reactor.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdint>
#include <sys/epoll.h>
#include <unistd.h>

RCR_TEST(PipeReadinessIsReported) {
  int pipe_fds[2]{-1, -1};
  RCR_REQUIRE(::pipe(pipe_fds) == 0);

  rcr::EpollReactor reactor;
  RCR_REQUIRE(reactor.valid());
  RCR_REQUIRE(reactor.add(pipe_fds[0], EPOLLIN).ok());
  const std::uint8_t byte = 0x5A;
  RCR_REQUIRE(::write(pipe_fds[1], &byte, sizeof(byte)) == 1);

  const auto ready = reactor.wait(std::chrono::milliseconds{50});
  RCR_REQUIRE(ready.ok());
  RCR_REQUIRE(ready.value().size() == 1);
  RCR_EXPECT(ready.value()[0].fd == pipe_fds[0]);
  RCR_EXPECT((ready.value()[0].events & EPOLLIN) != 0U);

  RCR_REQUIRE(reactor.remove(pipe_fds[0]).ok());
  ::close(pipe_fds[0]);
  ::close(pipe_fds[1]);
}

RCR_TEST(WaitTimeoutReturnsEmptySet) {
  rcr::EpollReactor reactor;
  const auto ready = reactor.wait(std::chrono::milliseconds{0});
  RCR_REQUIRE(ready.ok());
  RCR_EXPECT(ready.value().empty());
}

RCR_TEST(InvalidFdIsRejected) {
  rcr::EpollReactor reactor;
  const auto result = reactor.add(-1, EPOLLIN);
  RCR_EXPECT(!result.ok());
  RCR_EXPECT(result.error().code() == rcr::Errc::InvalidArgument);
}

RCR_TEST_MAIN()

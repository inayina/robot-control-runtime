// Linux 目标测试：OwnedFd / eventfd / signalfd 生命周期。
#include "rcr/epoll_reactor.hpp"
#include "rcr/owned_fd.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>

RCR_TEST(OwnedFdMoveTransfersOwnership) {
  int pipe_fds[2]{-1, -1};
  RCR_REQUIRE(::pipe(pipe_fds) == 0);
  ::close(pipe_fds[1]);

  rcr::OwnedFd first{pipe_fds[0]};
  RCR_REQUIRE(first.valid());
  rcr::OwnedFd second{std::move(first)};
  RCR_EXPECT(!first.valid());
  RCR_EXPECT(second.valid());
  RCR_EXPECT(second.get() == pipe_fds[0]);
  second.reset();
  RCR_EXPECT(!second.valid());
}

RCR_TEST(EventFdStopWakesEpollAndDrains) {
  auto event = rcr::EventFd::create();
  RCR_REQUIRE(event.ok());

  rcr::EpollReactor reactor;
  RCR_REQUIRE(reactor.valid());
  RCR_REQUIRE(reactor.add(event.value().native_handle(), EPOLLIN).ok());

  std::atomic<bool> woke{false};
  std::thread waiter([&] {
    const auto ready = reactor.wait(std::chrono::milliseconds{1000});
    if (ready && !ready.value().empty()) {
      woke.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  RCR_REQUIRE(event.value().signal_stop().ok());
  waiter.join();
  RCR_EXPECT(woke.load());

  auto drained = event.value().drain();
  RCR_REQUIRE(drained.ok());
  RCR_EXPECT(drained.value() >= 1);

  // 重复 stop 不应失败。
  RCR_REQUIRE(event.value().signal_stop().ok());
  RCR_REQUIRE(event.value().drain().ok());
  RCR_REQUIRE(reactor.remove(event.value().native_handle()).ok());
}

RCR_TEST(EventFdCreateStopCycleNoLeakGrowth) {
  // 粗测：反复创建/停止不应因 RAII 泄漏导致后续 create 失败。
  for (int i = 0; i < 100; ++i) {
    auto event = rcr::EventFd::create();
    RCR_REQUIRE(event.ok());
    RCR_REQUIRE(event.value().signal_stop().ok());
    RCR_REQUIRE(event.value().drain().ok());
  }
}

RCR_TEST(SignalFdReceivesBlockedSigterm) {
  auto signals = rcr::SignalFd::block_and_open_shutdown_signals();
  RCR_REQUIRE(signals.ok());

  // raise 向当前线程投递已屏蔽信号；比 kill 更不依赖沙箱对信号投递的限制。
  RCR_REQUIRE(::raise(SIGTERM) == 0);
  bool got = false;
  for (int i = 0; i < 50; ++i) {
    auto drained = signals.value().drain();
    RCR_REQUIRE(drained.ok());
    if (drained.value() > 0) {
      got = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  RCR_EXPECT(got);
}

RCR_TEST_MAIN()

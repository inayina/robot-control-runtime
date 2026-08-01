// Linux 目标测试；不依赖 MCU 工具链或真实 CAN 硬件。
#include "rcr/mailbox.hpp"
#include "test_support.hpp"

#include <thread>
#include <vector>

using rcr::CommandMailbox;
using rcr::OutputCommand;

RCR_TEST(PublishConsumeLatest) {
  CommandMailbox box;
  const OutputCommand first{.session_id = 1, .sequence = 1, .mask = 1, .values = 0};
  const OutputCommand latest{.session_id = 1, .sequence = 2, .mask = 1, .values = 1};

  box.publish(first);
  box.publish(latest);
  RCR_EXPECT(box.drop_count() == 1);

  const auto got = box.try_consume();
  RCR_REQUIRE(got.has_value());
  RCR_EXPECT(got->sequence == 2);
  RCR_EXPECT(got->values == 1);
  RCR_EXPECT(!box.has_pending());
  RCR_EXPECT(!box.try_consume().has_value());
}

RCR_TEST(PeekDoesNotConsume) {
  CommandMailbox box;
  box.publish(OutputCommand{.session_id = 1, .sequence = 7, .mask = 1});
  const auto peeked = box.peek();
  RCR_REQUIRE(peeked.has_value());
  RCR_EXPECT(peeked->sequence == 7);
  RCR_EXPECT(box.has_pending());
  RCR_EXPECT(box.try_consume()->sequence == 7);
}

RCR_TEST(ClearResetsSlot) {
  CommandMailbox box;
  box.publish(OutputCommand{.session_id = 1, .sequence = 3, .mask = 1});
  box.clear();
  RCR_EXPECT(!box.has_pending());
}

RCR_TEST(ConcurrentPublishersLeaveOneConsistentCommand) {
  CommandMailbox box;
  constexpr int kProducers = 4;
  constexpr int kEach = 200;
  std::vector<std::thread> threads;
  threads.reserve(kProducers);
  for (int producer = 0; producer < kProducers; ++producer) {
    threads.emplace_back([&box, producer]() {
      for (int index = 0; index < kEach; ++index) {
        box.publish(OutputCommand{
            .session_id = static_cast<std::uint64_t>(producer + 1),
            .sequence = static_cast<std::uint64_t>(index + 1),
            .mask = 1,
            .values = static_cast<std::uint32_t>(producer),
        });
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  RCR_EXPECT(box.publish_count() == static_cast<std::uint64_t>(kProducers * kEach));
  RCR_EXPECT(box.has_pending());
  const auto command = box.try_consume();
  RCR_REQUIRE(command.has_value());
  RCR_EXPECT(command->session_id >= 1 && command->session_id <= kProducers);
  RCR_EXPECT(command->sequence >= 1 && command->sequence <= kEach);
}

RCR_TEST_MAIN()

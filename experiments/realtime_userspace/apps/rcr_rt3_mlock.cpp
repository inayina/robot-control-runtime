// RT3-M：mlockall + 工作集预热 vs 冷触碰的缺页差异。
// 用 mmap(MAP_ANONYMOUS) + getrusage.ru_minflt（本机 status Minflt 可能不及时更新）。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {

struct FaultSnapshot {
  long minflt{0};
  long majflt{0};
};

FaultSnapshot read_faults() {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  return FaultSnapshot{ru.ru_minflt, ru.ru_majflt};
}

void touch_pages(std::uint8_t* data, std::size_t bytes) {
  const long page = sysconf(_SC_PAGESIZE);
  const std::size_t step = page > 0 ? static_cast<std::size_t>(page) : 4096U;
  for (std::size_t i = 0; i < bytes; i += step) {
    data[i] = static_cast<std::uint8_t>(i & 0xff);
  }
  if (bytes > 0) {
    data[bytes - 1] ^= 1;
  }
}

std::uint8_t* map_anon(std::size_t bytes) {
  void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    return nullptr;
  }
  return static_cast<std::uint8_t*>(p);
}

struct Options {
  std::size_t bytes{16 * 1024 * 1024};
  bool self_check{false};
};

bool parse(int argc, char** argv, Options& opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--self-check") {
      opt.self_check = true;
      continue;
    }
    if (a == "--help" || a == "-h") {
      return false;
    }
    if (i + 1 >= argc) {
      return false;
    }
    if (a == "--bytes") {
      opt.bytes = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
    } else {
      return false;
    }
  }
  return opt.bytes >= 4096 && opt.bytes <= (512ULL * 1024 * 1024);
}

}  // namespace

int main(int argc, char** argv) {
  Options opt{};
  if (!parse(argc, argv, opt)) {
    std::cerr << "usage: " << argv[0] << " [--bytes N] [--self-check]\n";
    return EXIT_FAILURE;
  }

  std::cout << "experiment=rt3_mlock\n"
            << "bytes=" << opt.bytes << "\n"
            << "fault_source=getrusage_ru_minflt\n";

  std::uint8_t* cold = map_anon(opt.bytes);
  if (cold == nullptr) {
    std::cout << "result=failed\n"
              << "detail=mmap_cold_failed\n";
    return EXIT_FAILURE;
  }
  const auto before_cold = read_faults();
  touch_pages(cold, opt.bytes);
  const auto after_cold = read_faults();
  const long cold_min = after_cold.minflt - before_cold.minflt;
  const long cold_maj = after_cold.majflt - before_cold.majflt;
  std::cout << "phase=cold_touch\n"
            << "delta_minflt=" << cold_min << "\n"
            << "delta_majflt=" << cold_maj << "\n";
  ::munmap(cold, opt.bytes);

  std::uint8_t* warm = map_anon(opt.bytes);
  if (warm == nullptr) {
    std::cout << "result=failed\n"
              << "detail=mmap_warm_failed\n";
    return EXIT_FAILURE;
  }
  touch_pages(warm, opt.bytes);
  const int lock_rc = ::mlockall(MCL_CURRENT | MCL_FUTURE);
  const int lock_errno = lock_rc == 0 ? 0 : errno;
  std::cout << "mlockall_ok=" << (lock_rc == 0 ? 1 : 0) << "\n"
            << "mlockall_errno=" << lock_errno << "\n";

  const auto before_locked = read_faults();
  touch_pages(warm, opt.bytes);
  const auto after_locked = read_faults();
  const long locked_min = after_locked.minflt - before_locked.minflt;
  const long locked_maj = after_locked.majflt - before_locked.majflt;
  std::cout << "phase=locked_retouch\n"
            << "delta_minflt=" << locked_min << "\n"
            << "delta_majflt=" << locked_maj << "\n";
  ::munmap(warm, opt.bytes);

  if (lock_rc != 0) {
    std::cout << "result=permission_denied\n"
              << "unsupported_reason=mlockall_failed\n";
    return opt.self_check ? 77 : EXIT_FAILURE;
  }

  std::cout << "result=pass\n";
  if (opt.self_check) {
    if (cold_min <= 0) {
      std::cerr << "self-check failed: cold_touch expected minflt>0\n";
      return EXIT_FAILURE;
    }
    if (locked_min > cold_min / 2 && locked_min > 64) {
      std::cerr << "self-check failed: locked retouch still high minflt\n";
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

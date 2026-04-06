// osx system info
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <system_error>
#include <vector>

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

enum class cpu_brand {
  unknown,
  m1,
  m2,
  m3,
  m4,
  m5,
};

// cpu / gpu dynamic freq
struct xpu_dynamic_freq {
  // Frequence (in MHZ)
  uint32_t freq = 0;
};

class sys_info {
 public:
  auto init() -> std::error_code;

  auto cpu_brand() const { return cpu_brand_; }

  const auto& e_cpu_dynamic_freq_list() const { return e_cpu_dynamic_freq_list_; }

  const auto& p_cpu_dynamic_freq_list() const { return p_cpu_dynamic_freq_list_; }

  const auto& s_cpu_dynamic_freq_list() const { return s_cpu_dynamic_freq_list_; }

  const auto& gpu_dynamic_freq_list() const { return gpu_dynamic_freq_list_; }

  auto memory_total_size() const { return memory_total_size_; }

  auto memory_page_size() const { return memory_page_size_; }

 private:
  hwlcd::hwinfo::detail::osx::cpu_brand cpu_brand_ = hwlcd::hwinfo::detail::osx::cpu_brand::unknown;
  size_t e_cpu_core_num_ = 0;
  size_t p_cpu_core_num_ = 0;
  std::vector<xpu_dynamic_freq> e_cpu_dynamic_freq_list_;
  std::vector<xpu_dynamic_freq> p_cpu_dynamic_freq_list_;
  std::vector<xpu_dynamic_freq> s_cpu_dynamic_freq_list_;
  std::vector<xpu_dynamic_freq> gpu_dynamic_freq_list_;
  uint64_t memory_total_size_ = 0;  // In bytes
  uint64_t memory_page_size_ = 0;   // In bytes
};

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif

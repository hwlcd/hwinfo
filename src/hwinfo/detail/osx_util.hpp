// Osx utility
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

struct mach_error_category : std::error_category {
  const char* name() const noexcept override { return "mach"; }
  std::string message(int ev) const override { return mach_error_string(ev); }
};

inline const auto& mach_category() {
  static mach_error_category c;
  return c;
}

inline auto make_mach_error_code(kern_return_t ec) { return std::error_code(ec, mach_category()); }

template <size_t S, size_t MibSize, typename F>
auto sysctl_dynamic_buffer(const std::array<int, MibSize>& mib, F&& f) -> std::error_code {
  int ec = 0;
  size_t needed_size = 0;
  uint miblen = mib.size();
  if (ec = sysctl((int*)mib.data(), miblen, NULL, &needed_size, NULL, 0); ec) {
    return std::error_code(ec, std::system_category());
  }
  if (needed_size == 0) {
    return std::make_error_code(std::errc::no_message);
  }

  char static_buffer[S];
  if (needed_size <= S) {
    // Use static buffer
    size_t buffer_len = S;
    if (ec = sysctl((int*)mib.data(), miblen, static_buffer, &buffer_len, NULL, 0); ec) {
      return std::error_code(ec, std::system_category());
    }
    return f((const char*)static_buffer, needed_size);
  } else {
    // Use dynamic buffer
    std::unique_ptr<char[]> p_buffer(new char[needed_size]);
    size_t buffer_len = needed_size;
    if (ec = sysctl((int*)mib.data(), miblen, p_buffer.get(), &buffer_len, NULL, 0); ec) {
      return std::error_code(ec, std::system_category());
    }
    return f((const char*)p_buffer.get(), needed_size);
  }
}

template <typename T>
auto sysctlbyname_scalar(std::string_view sv, T& out_value) -> std::error_code {
  size_t buf_size = sizeof(T);
  auto ec = sysctlbyname(sv.data(), &out_value, &buf_size, NULL, 0);
  if (ec) {
    return std::error_code(ec, std::system_category());
  }
  return std::error_code();
}

template <size_t S>
auto sysctlbyname_string(std::string_view sv, char str[S]) -> std::error_code {
  if (str == nullptr) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  size_t buf_size = S;
  auto ec = sysctlbyname(sv.data(), str, &buf_size, NULL, 0);
  if (ec) {
    return std::error_code(ec, std::system_category());
  }
  return std::error_code();
}

class cf_string {
 public:
  cf_string() : ref_(NULL) {}

  cf_string(const char* str) : cf_string() {
    auto size = strlen(str);
    if (size > 0) {
      ref_ = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault, (const UInt8*)str, size, kCFStringEncodingUTF8, 0,
                                           kCFAllocatorNull);
    }
  }

  cf_string(const char* str, size_t size) : cf_string() {
    if (size > 0) {
      ref_ = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault, (const UInt8*)str, size, kCFStringEncodingUTF8, 0,
                                           kCFAllocatorNull);
    }
  }

  cf_string(const std::string_view& sv) : cf_string() {
    if (sv.size() > 0) {
      ref_ = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault, (const UInt8*)sv.data(), sv.size(),
                                           kCFStringEncodingUTF8, 0, kCFAllocatorNull);
    }
  }

  ~cf_string() {
    if (ref_) {
      CFRelease(ref_);
      ref_ = NULL;
    }
  }

  cf_string(const cf_string&) = delete;
  cf_string(cf_string&& other) : ref_(other.ref_) { other.ref_ = NULL; }

  cf_string& operator=(const cf_string&) = delete;
  cf_string& operator=(cf_string&& other) {
    if (ref_) {
      CFRelease(ref_);
    }
    ref_ = other.ref_;
    other.ref_ = NULL;
    return *this;
  }

  CFStringRef ref() const { return ref_; }

 private:
  CFStringRef ref_;
};

template <size_t S = 128>
auto from_cf_string(CFStringRef ref) -> std::string {
  if (ref) {
    char buffer[S];
    if (CFStringGetCString(ref, buffer, S, kCFStringEncodingUTF8)) {
      return std::string(buffer, strnlen(buffer, S));
    }
  }
  return std::string();
}

inline auto cf_get_dict_value(CFDictionaryRef ref, const std::string_view& sv) -> CFTypeRef {
  if (ref) {
    cf_string key(sv);
    return CFDictionaryGetValue(ref, key.ref());
  }
  return NULL;
}

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif

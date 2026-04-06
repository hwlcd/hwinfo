// Utility
#pragma once

#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {

template <typename F>
struct scope_guard {
  F f;
  template <typename T>
  explicit scope_guard(T&& f) : f(std::move(f)) {}
  ~scope_guard() { f(); }
  scope_guard(const scope_guard&) = delete;
  scope_guard& operator=(const scope_guard&) = delete;
};

template <typename F>
auto make_scope_guard(F&& f) {
  return scope_guard<std::decay_t<F>>(std::forward<F>(f));
}

template <typename T>
struct metric_value_traits {
  static auto get_value(const T& v) { return v; }
};

template <typename T>
struct metric_value_traits<sequential_metric<T>> {
  static auto get_value(const sequential_metric<T>& v) { return v.value; }
};

template <typename T>
struct metric_value_traits<named_metric<T>> {
  static auto get_value(const named_metric<T>& v) { return v.value; }
};

template <typename T>
struct metric_value_traits<device_metric<T>> {
  static auto get_value(const device_metric<T>& v) { return v.value; }
};

template <typename T>
struct metric_value_traits<device_sequential_metric<T>> {
  static auto get_value(const device_sequential_metric<T>& v) { return v.value; }
};

template <typename T, typename BeginIt, typename EndIt>
auto stat_values(BeginIt&& begin, EndIt&& end) -> stats_value<T> {
  if (begin == end) {
    return stats_value<T>();
  }
  // Run stats
  size_t num = 1;
  auto begin_value = metric_value_traits<std::decay_t<decltype(*begin)>>::get_value(*begin);
  stats_value<T> stats{begin_value, begin_value, begin_value};
  auto it = begin;
  for (++it; it != end; ++it) {
    ++num;
    auto value = metric_value_traits<std::decay_t<decltype(*it)>>::get_value(*it);
    if (value > stats.max) {
      stats.max = value;
    }
    if (value < stats.min) {
      stats.min = value;
    }
    stats.avg += value;
  }
  // Average
  stats.avg /= num;
  return stats;
}

}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

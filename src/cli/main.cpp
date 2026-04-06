// Cli main

#include <boost/json.hpp>
#include <boost/mp11.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <deque>
#include <iomanip>
#include <iostream>
#include <thread>

#if defined(__APPLE__) || defined(__linux__)
#include <ncurses.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "hwinfo/hwinfo.hpp"

namespace mp11 = boost::mp11;
namespace po = boost::program_options;
namespace json = boost::json;
namespace describe = boost::describe;

using namespace std::chrono_literals;
using namespace hwlcd::hwinfo;

constexpr auto cmd_line_style = po::command_line_style::default_style & ~po::command_line_style::allow_guessing;

// Global states
bool stop = false;

// Command line args
bool use_json = false;
bool use_text = false;
size_t output_number = 0;

// Overload the json serialize method of std types
namespace std {
void tag_invoke(const json::value_from_tag&, json::value& jv, const error_code& ec);
namespace chrono {
void tag_invoke(const json::value_from_tag&, json::value& jv, const system_clock::time_point& t);
void tag_invoke(const json::value_from_tag&, json::value& jv, const system_clock::duration& d);
}  // namespace chrono
}  // namespace std

class sampler {
 public:
  auto init() -> std::error_code;
  template <typename F>
  void sample(F&& f);

 private:
  hwlcd::hwinfo::hwinfo info_;
  std::deque<hwlcd::hwinfo::sample> sample_queue_;
};

//
// Outputs
//

class output_base {
 public:
  explicit output_base(sampler& sampler) : sampler_(sampler) {}
  virtual ~output_base() = default;
  virtual void run() = 0;

 protected:
  auto get_sampler() -> sampler& { return sampler_; }

 private:
  sampler& sampler_;
};

//
// Terminal output is not available on windows
//
#if defined(__APPLE__) || defined(__linux__)

class terminal_output : public output_base {
 public:
  explicit terminal_output(sampler& sampler) : output_base(sampler) {}
  virtual ~terminal_output() = default;
  void run() override;

 private:
  std::deque<sample_diff> diff_queue_;

  enum class display_type {
    tiny,
    middle,
    large,
  };
  display_type display_type_ = display_type::tiny;

  enum class color : short {
    grey = 1,
  };
  enum class color_style : short {
    primary = 1,
    secondary,
  };
  bool has_colors_ = false;
  // Info tab index
  int tab_index_ = 1;

  struct middle_windows {
    WINDOW* top;
    WINDOW* bottom;

    middle_windows(WINDOW* top, WINDOW* bottom) : top(top), bottom(bottom) {}

    middle_windows(const middle_windows&) = delete;
    middle_windows(middle_windows&&) = default;

    ~middle_windows() {
      if (top) {
        delwin(top);
        top = nullptr;
      }
      if (bottom) {
        delwin(bottom);
        bottom = nullptr;
      }
    }
  };

  struct large_windows {
    WINDOW* top_left;
    WINDOW* top_right;
    WINDOW* bottom_left;
    WINDOW* bottom_right;

    large_windows(WINDOW* top_left, WINDOW* top_right, WINDOW* bottom_left, WINDOW* bottom_right)
        : top_left(top_left), top_right(top_right), bottom_left(bottom_left), bottom_right(bottom_right) {}

    large_windows(const large_windows&) = delete;
    large_windows(large_windows&&) = default;

    ~large_windows() {
      if (top_left) {
        delwin(top_left);
        top_left = nullptr;
      }
      if (top_right) {
        delwin(top_right);
        top_right = nullptr;
      }
      if (bottom_left) {
        delwin(bottom_left);
        bottom_left = nullptr;
      }
      if (bottom_right) {
        delwin(bottom_right);
        bottom_right = nullptr;
      }
    }
  };

  std::variant<short, middle_windows, large_windows> windows_ = (short)0;

  void draw(bool resize);
  void draw_tiny();
  void draw_middle(bool resize);
  void draw_large(bool resize);
  void draw_window_border(WINDOW* p_win, std::string_view title);
  void draw_bar_chart(WINDOW* p_win, std::string_view y_max_label, std::string_view y_min_label,
                      const std::vector<float>& values);
  void draw_cpu_usage_window(WINDOW* p_win);
  void draw_cpu_freq_window(WINDOW* p_win);
  void draw_gpu_freq_window(WINDOW* p_win);
  void draw_memory_usage_window(WINDOW* p_win);
  void draw_disk_io_read_rate_window(WINDOW* p_win);
  void draw_disk_io_write_rate_window(WINDOW* p_win);
  void draw_network_io_read_rate_window(WINDOW* p_win);
  void draw_network_io_write_rate_window(WINDOW* p_win);
  void draw_cpu_temperature_window(WINDOW* p_win);
  void draw_gpu_temperature_window(WINDOW* p_win);
  void draw_memory_temperature_window(WINDOW* p_win);
  void draw_disk_temperature_window(WINDOW* p_win);
  void draw_battery_temperature_window(WINDOW* p_win);
  void draw_fan_speed_window(WINDOW* p_win);
  auto get_display_type() -> display_type;
};

#endif

class text_output : public output_base {
 public:
  explicit text_output(sampler& sampler) : output_base(sampler) {}
  virtual ~text_output() = default;
  void run() override;
};

class json_output : public output_base {
 public:
  explicit json_output(sampler& sampler) : output_base(sampler) {}
  virtual ~json_output() = default;
  void run() override;
};

void handle_signal(int);

//
// ------ Main entry
//

int main(int argc, const char** argv) {
  po::options_description desc("hwinfo command line tool\nAllowed options");
  desc.add_options()("help", "Show help message")                          //
      ("json,j", po::bool_switch(&use_json), "Use json as output format")  //
      ("text,t", po::bool_switch(&use_text),
       "Use text as output format (even if stdout is attached with a tty). Please note output data will be in json "
       "format when both json and text options are specified.")  //
      ("number,n", po::value<std::size_t>(&output_number),
       "Output report number (report is updated every 5s), use 0 for infinite, default is 0.");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc, cmd_line_style), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  // Create sampler
  sampler sampler;
  if (auto ec = sampler.init(); ec) {
    std::cerr << "Init sampler failed: " << ec.message() << std::endl;
    return -1;
  }

  // Create output
  auto p_output = [&]() -> std::unique_ptr<output_base> {
    if (use_json) {
      return std::make_unique<json_output>(sampler);
    } else if (use_text) {
      return std::make_unique<text_output>(sampler);
    } else {
      //
      // Use terminal type of stdout is attached with a tty
      //
#if defined(__APPLE__) || defined(__linux__)
      if (isatty(fileno(stdout))) {
        return std::make_unique<terminal_output>(sampler);
      } else {
        return std::make_unique<text_output>(sampler);
      }
#else
      // Terminal output is not available on windows
      return std::make_unique<text_output>();
#endif
    }
  }();
  if (p_output == nullptr) {
    return -1;
  }

  // Install signal handler
  {
#if defined(__APPLE__) || defined(__linux__)
    struct sigaction signal_action{{.__sa_handler = &handle_signal}, .sa_flags = 0, .sa_mask = 0};
    sigemptyset(&signal_action.sa_mask);
    // Handle ctrl + c signal
    if (auto ec = sigaction(SIGINT, &signal_action, NULL); ec) {
      std::cerr << "Init signal handler failed: " << strerror(ec) << std::endl;
      return -1;
    }
    // Handle ctrl + z signal
    if (auto ec = sigaction(SIGTSTP, &signal_action, NULL); ec) {
      std::cerr << "Init signal handler failed: " << strerror(ec) << std::endl;
      return -1;
    }
#else
    // TODO: Support windows
#endif
  }

  p_output->run();
  return 0;
}

void handle_signal(int signal) {
  switch (signal) {
  case SIGTSTP:
  case SIGINT:
    // Exit
    stop = true;
    break;
  }
}

// ------ Implement sampler

auto sampler::init() -> std::error_code {
  // Init hwinfo
  return info_.init();
}

template <typename F>
void sampler::sample(F&& f) {
  sample_queue_.emplace_back(info_.sample(sample_queue_.size() % 5 == 0));
  if (sample_queue_.size() == 6) {
    // Calculate sample diff
    f(info_.sample_diff(sample_queue_.begin(), sample_queue_.end()));
    for (size_t i = 0; i < 5; ++i) {
      sample_queue_.pop_front();
    }
  }
}

// ------ Implement terminal output
// Windows is not supported

#if defined(__APPLE__) || defined(__linux__)

void terminal_output::run() {
  // Init ncurses
  initscr();
  has_colors_ = has_colors();
  if (has_colors_) {
    start_color();
    use_default_colors();
    // Define colors and styles
    init_color((short)color::grey, 500, 500, 500);
    init_pair((short)color_style::primary, COLOR_BLUE, -1);
    init_pair((short)color_style::secondary, (short)color::grey, -1);
  }
  cbreak();               // Disable line buffer
  noecho();               // Don't echo
  curs_set(0);            // Hide cursor
  nodelay(stdscr, TRUE);  // No delay
  // Init display type
  display_type_ = get_display_type();
  // Initial display message
  printw("Initial sampling...");
  refresh();
  // Main loop
  size_t current_number = 0;
  size_t sleep_times = 0;
  while (!stop) {
    auto ch = wgetch(stdscr);
    if (ch == ERR) {
      // No more chars
      if (sleep_times >= 10) {
        sleep_times = 0;
        get_sampler().sample([&](sample_diff&& diff) {
          ++current_number;
          diff_queue_.emplace_back(std::move(diff));
          if (diff_queue_.size() > 1024) {
            diff_queue_.pop_front();
          }
          // Draw
          draw(false);
        });
      }
      if (stop || (output_number > 0 && current_number >= output_number)) {
        break;
      }
      ++sleep_times;
      std::this_thread::sleep_for(100ms);
    } else if (ch == KEY_RESIZE) {
      // Redraw at once
      draw(true);
    } else if (ch == 'q') {
      // Exit
      stop = true;
      break;
    } else if (ch >= '0' && ch <= '9') {
      // Update tab index and redraw immediately
      tab_index_ = ch - '0';
      draw(false);
    }
  }
  // End
  endwin();
}

void terminal_output::draw(bool resize) {
  auto new_display_type = get_display_type();
  if (display_type_ != new_display_type) {
    // Change display type, reset tab index
    display_type_ = new_display_type;
    tab_index_ = 1;
  }
  if (diff_queue_.size() > 0) {
    switch (display_type_) {
    default:
    case display_type::tiny:
      draw_tiny();
      break;
    case display_type::middle:
      draw_middle(resize);
      break;
    case display_type::large:
      draw_large(resize);
      break;
    }
  }
}

void terminal_output::draw_tiny() {
  if (auto v = std::get_if<short>(&windows_); !v) {
    windows_ = (short)0;
  }
  clear();
  // Draw title line
  mvprintw(0, 0, "HWINFO ");
  if (has_colors_) attron(COLOR_PAIR((short)color_style::secondary));
  mvprintw(0, 7, "| Press 1/2/3/... to switch info, q or ctrl+c to exit.");
  mvprintw(1, 0, "============================================================");
  if (has_colors_) attroff(COLOR_PAIR((short)color_style::secondary));

  auto draw_info_line = [&](int row, std::string_view title, const char* format, auto&&... args) {
    if (has_colors_) attron(COLOR_PAIR((short)color_style::primary));
    mvaddstr(row, 0, title.data());
    if (has_colors_) attroff(COLOR_PAIR((short)color_style::primary));
    mvprintw(row, title.size(), format, args...);
  };

  auto draw_stats_line = [&](int row, std::string_view title, const stats_value<float>& value) {
    const auto texts = std::make_tuple(std::format("AVG={:.2f}", value.avg), std::format("MIN={:.2f}", value.min),
                                       std::format("MAX={:.2f}", value.max));
    if (has_colors_) attron(COLOR_PAIR((short)color_style::primary));
    mvaddstr(row, 0, title.data());
    if (has_colors_) attroff(COLOR_PAIR((short)color_style::primary));
    mvprintw(row, title.size(), "  AVG=%.2f  MIN=%.2f  MAX=%.2f", value.avg, value.min, value.max);
  };

  const auto& latest_diff = diff_queue_.back();
  switch (tab_index_) {
  default:
  case 1: {
    // CPU
    int row = 1;
    draw_info_line(++row, "CPU Usage (%):", "  %.2f", latest_diff.cpu_usage * 100);
    draw_stats_line(++row, "CPU Frequency (mhz):", latest_diff.cpu_freq);
    draw_stats_line(++row, "CPU Temperature (C):", latest_diff.cpu_temperature);
    break;
  }
  case 2: {
    // GPU
    int row = 1;
    draw_info_line(++row, "GPU Frequency (mhz):", "  %.2f", latest_diff.gpu_freq);
    draw_stats_line(++row, "GPU Temperature (C):", latest_diff.gpu_temperature);
    break;
  }
  case 3: {
    // Memory
    int row = 1;
    draw_info_line(++row, "Memory Usage (%):", "  %.2f", latest_diff.memory_usage * 100);
    draw_info_line(++row, "Memory Available (%):", "  %.2f", latest_diff.memory_available_percentage * 100);
    draw_info_line(++row, "Memory Free (%):", "  %.2f", latest_diff.memory_free_percentage * 100);
    draw_stats_line(++row, "Memory Temperature (C):", latest_diff.memory_temperature);
    break;
  }
  case 4: {
    // Disk
    int row = 1;
    draw_info_line(++row, "Disk Total IO (byte/s):", "  Read=%.2f  Write=%.2f",
                   latest_diff.total_disk_io_rate.read_bytes, latest_diff.total_disk_io_rate.write_bytes);
    for (const auto& [name, rate] : latest_diff.disk_io_rate_per_disk) {
      draw_info_line(++row, std::format("Disk IO [{}] (byte/s):", name), "  Read=%.2f  Write=%.2f", rate.read_bytes,
                     rate.write_bytes);
    }
    break;
  }
  case 5: {
    // Network
    int row = 1;
    draw_info_line(++row, "Network Total IO (byte/s):", "  Read=%.2f  Write=%.2f",
                   latest_diff.total_net_io_rate.read_bytes, latest_diff.total_net_io_rate.write_bytes);
    for (const auto& [name, rate] : latest_diff.net_io_rate_per_if) {
      draw_info_line(++row, std::format("Network IO [{}] (byte/s):", name), "  Read=%.2f  Write=%.2f", rate.read_bytes,
                     rate.write_bytes);
    }
    break;
  }
  case 6: {
    // Power consumption
    int row = 1;
    mp11::mp_for_each<describe::describe_enumerators<device_type>>([&](auto D) {
      auto dt = static_cast<device_type>(D.value);
      auto it = latest_diff.power_consumptions.find(dt);
      if (it != latest_diff.power_consumptions.end()) {
        draw_info_line(++row, std::format("Device [{}] (w):", D.name), "  %.2f", it->second);
      }
    });
    break;
  }
  case 7: {
    // All temperatures & fan speed
    int row = 1;
    for (const auto& m : latest_diff.temperatures) {
      draw_info_line(
          ++row, std::format("Device [{}] #{} Temperature (C):", describe::enum_to_string(m.device, "unknown"), m.num),
          "  %.2f", m.value);
    }
    for (const auto& m : latest_diff.fan_speeds) {
      draw_info_line(++row, std::format("Fan #{} Speed (rpm):", m.num), "  %.2f", m.value);
    }
    break;
  }
  }

  refresh();
}

void terminal_output::draw_middle(bool resize) {
  auto p_windows = std::get_if<middle_windows>(&windows_);
  if (!p_windows || resize) {
    // Init windows
    clear();
    // Draw title line
    mvprintw(0, 0, "HWINFO ");
    if (has_colors_) attron(COLOR_PAIR((short)color_style::secondary));
    mvprintw(0, 7, "| Press 1/2/3/... to switch info, q or ctrl+c to exit.");
    if (has_colors_) attroff(COLOR_PAIR((short)color_style::secondary));
    refresh();

    // Calculate window size
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int top_height = max_y / 2;
    int bottom_height = max_y - top_height - 1;

    if (!p_windows) {
      // Create two windows
      auto win_top = newwin(top_height, max_x, 1, 0);
      auto win_bottom = newwin(bottom_height, max_x, top_height + 1, 0);
      windows_.emplace<middle_windows>(win_top, win_bottom);
      p_windows = std::get_if<middle_windows>(&windows_);
      if (!p_windows) {
        return;
      }
    } else {
      // Move and resize windows
      mvwin(p_windows->bottom, top_height + 1, 0);
      wresize(p_windows->top, top_height, max_x);
      wresize(p_windows->bottom, bottom_height, max_x);
    }
  }

  switch (tab_index_) {
  default:
  case 1: {
    if (p_windows->top) {
      draw_cpu_usage_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_cpu_freq_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 2: {
    if (p_windows->top) {
      draw_gpu_freq_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_memory_usage_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 3: {
    if (p_windows->top) {
      draw_disk_io_read_rate_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_disk_io_write_rate_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 4: {
    if (p_windows->top) {
      draw_network_io_read_rate_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_network_io_write_rate_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 5: {
    if (p_windows->top) {
      draw_cpu_temperature_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_gpu_temperature_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 6: {
    if (p_windows->top) {
      draw_memory_temperature_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_disk_temperature_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  case 7: {
    if (p_windows->top) {
      draw_battery_temperature_window(p_windows->top);
      wrefresh(p_windows->top);
    }
    if (p_windows->bottom) {
      draw_fan_speed_window(p_windows->bottom);
      wrefresh(p_windows->bottom);
    }
    break;
  }
  }
}

void terminal_output::draw_large(bool resize) {
  auto p_windows = std::get_if<large_windows>(&windows_);
  if (!p_windows || resize) {
    // Init windows
    clear();
    // Draw title line
    mvprintw(0, 0, "HWINFO ");
    if (has_colors_) attron(COLOR_PAIR((short)color_style::secondary));
    mvprintw(0, 7, "| Press 1/2/3/... to switch info, q or ctrl+c to exit.");
    if (has_colors_) attroff(COLOR_PAIR((short)color_style::secondary));
    refresh();

    // Calculate window size
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int top_height = max_y / 2;
    int left_width = max_x / 2;
    int bottom_height = max_y - top_height - 1;
    int right_width = max_x - left_width - 1;

    if (!p_windows) {
      // Create two windows
      auto win_top_left = newwin(top_height, left_width, 1, 0);
      auto win_top_right = newwin(top_height, right_width, 1, left_width);
      auto win_bottom_left = newwin(bottom_height, left_width, top_height + 1, 0);
      auto win_bottom_right = newwin(bottom_height, right_width, top_height + 1, left_width);
      windows_.emplace<large_windows>(win_top_left, win_top_right, win_bottom_left, win_bottom_right);
      p_windows = std::get_if<large_windows>(&windows_);
      if (!p_windows) {
        return;
      }
    } else {
      // Move and resize windows
      mvwin(p_windows->top_right, 1, left_width);
      mvwin(p_windows->bottom_left, top_height + 1, 0);
      mvwin(p_windows->bottom_right, top_height + 1, left_width);
      wresize(p_windows->top_left, top_height, left_width);
      wresize(p_windows->top_right, top_height, right_width);
      wresize(p_windows->bottom_left, bottom_height, left_width);
      wresize(p_windows->bottom_right, bottom_height, right_width);
    }
  }

  switch (tab_index_) {
  default:
  case 1: {
    if (p_windows->top_left) {
      draw_cpu_usage_window(p_windows->top_left);
      wrefresh(p_windows->top_left);
    }
    if (p_windows->top_right) {
      draw_cpu_freq_window(p_windows->top_right);
      wrefresh(p_windows->top_right);
    }
    if (p_windows->bottom_left) {
      draw_gpu_freq_window(p_windows->bottom_left);
      wrefresh(p_windows->bottom_left);
    }
    if (p_windows->bottom_right) {
      draw_memory_usage_window(p_windows->bottom_right);
      wrefresh(p_windows->bottom_right);
    }
    break;
  }
  case 2: {
    if (p_windows->top_left) {
      draw_disk_io_read_rate_window(p_windows->top_left);
      wrefresh(p_windows->top_left);
    }
    if (p_windows->top_right) {
      draw_disk_io_write_rate_window(p_windows->top_right);
      wrefresh(p_windows->top_right);
    }
    if (p_windows->bottom_left) {
      draw_network_io_read_rate_window(p_windows->bottom_left);
      wrefresh(p_windows->bottom_left);
    }
    if (p_windows->bottom_right) {
      draw_network_io_write_rate_window(p_windows->bottom_right);
      wrefresh(p_windows->bottom_right);
    }
    break;
  }
  case 3: {
    if (p_windows->top_left) {
      draw_cpu_temperature_window(p_windows->top_left);
      wrefresh(p_windows->top_left);
    }
    if (p_windows->top_right) {
      draw_gpu_temperature_window(p_windows->top_right);
      wrefresh(p_windows->top_right);
    }
    if (p_windows->bottom_left) {
      draw_memory_temperature_window(p_windows->bottom_left);
      wrefresh(p_windows->bottom_left);
    }
    if (p_windows->bottom_right) {
      draw_disk_temperature_window(p_windows->bottom_right);
      wrefresh(p_windows->bottom_right);
    }
    break;
  }
  case 4: {
    if (p_windows->top_left) {
      draw_battery_temperature_window(p_windows->top_left);
      wrefresh(p_windows->top_left);
    }
    if (p_windows->top_right) {
      draw_fan_speed_window(p_windows->top_right);
      wrefresh(p_windows->top_right);
    }
    if (p_windows->bottom_left) {
      wclear(p_windows->bottom_left);
      wrefresh(p_windows->bottom_left);
    }
    if (p_windows->bottom_right) {
      wclear(p_windows->bottom_right);
      wrefresh(p_windows->bottom_right);
    }
    break;
  }
  }
}

void terminal_output::draw_window_border(WINDOW* p_win, std::string_view title) {
  if (!p_win) {
    return;
  }
  auto max_x = getmaxx(p_win);
  if (max_x < 3) {
    return;
  }
  box(p_win, 0, 0);
  if (title.size() > (size_t)max_x - 2) {
    mvwprintw(p_win, 0, 0, "%s...", title.substr(0, max_x - 3));
  } else {
    mvwprintw(p_win, 0, std::max((max_x - (int)(title.size() + 1)) / 2, 0), " %s ", title);
  }
}

void terminal_output::draw_bar_chart(WINDOW* p_win, std::string_view y_max_label, std::string_view y_min_label,
                                     const std::vector<float>& values) {
  if (!p_win || values.size() == 0 || diff_queue_.size() == 0) {
    return;
  }
  int max_y, max_x;
  getmaxyx(p_win, max_y, max_x);
  // |hh:mm:ss|
  if (max_x < 9) {
    return;
  }
  // Get bar max height
  //  =====       <-- Top border
  //
  //
  //  00:10:32    <-- x_label
  //  =====       <-- Bottom border
  int bar_max_height = max_y - 3 - 1;  // Minus 1 because I leave 1 block for lower bound value
  if (bar_max_height <= 0) {
    return;
  }
  float bar_height_unit = (float)bar_max_height / 1.0;
  int max_y_label_size = std::max(y_max_label.size(), y_min_label.size());
  //  Get chart max width
  //
  //               |  Width  |
  //  | y_max_label           y_max_label |
  //  |
  //  | y_min_label           y_min_label |
  //
  int chart_max_width =
      max_x - max_y_label_size * 2 - 2 - 2;  // -2 for box boundary, -2 for white-space between label and bar
  if (chart_max_width <= 0) {
    return;
  }
  // Draw y label
  mvwaddstr(p_win, 1, 1, y_max_label.data());
  mvwaddstr(p_win, 1, max_x - y_max_label.size() - 1, y_max_label.data());
  mvwaddstr(p_win, max_y - 3, 1, y_min_label.data());
  mvwaddstr(p_win, max_y - 3, max_x - y_min_label.size() - 1, y_min_label.data());
  // Get the index of left most bar
  size_t index = values.size() > (size_t)chart_max_width ? values.size() - chart_max_width : 0;
  // Draw x label by index
  mvwaddstr(p_win, max_y - 2, 1,
            std::format("-{:%T}", std::chrono::duration_cast<std::chrono::seconds>(diff_queue_.back().end_time -
                                                                                   diff_queue_[index].start_time))
                .c_str());
  mvwaddstr(p_win, max_y - 2, max_x - 4, "now");
  // Draw chart
  wattron(p_win, A_REVERSE);
  int x = max_y_label_size + 1;  // A one white-space
  while (index < values.size()) {
    ++x;
    int height =
        std::min((int)round(values[index] * bar_height_unit), bar_max_height) + 1;  // Plus 1 for lower bound value
    mvwvline(p_win, max_y - height - 2, x, ' ', height);
    ++index;
  }
  wattroff(p_win, A_REVERSE);
}

template <typename T, typename BeginIt, typename EndIt>
void build_timeline_data(BeginIt&& begin, EndIt&& end, std::vector<float>* p_out_values, T* p_out_max_value,
                         T* p_out_min_value) {
  if (!p_out_values || !p_out_max_value || !p_out_min_value) {
    return;
  }
  size_t count = 0;
  *p_out_max_value = 0;
  *p_out_min_value = 0;
  for (auto it = begin; it != end; ++it, ++count) {
    auto value = *it;
    if (*p_out_max_value == 0 || value > *p_out_max_value) {
      *p_out_max_value = value;
    }
    if (*p_out_min_value == 0 || value < *p_out_min_value) {
      *p_out_min_value = value;
    }
  }
  float value_range = static_cast<float>(*p_out_max_value - *p_out_min_value);
  p_out_values->reserve(count);
  for (auto it = begin; it != end; ++it) {
    p_out_values->push_back(value_range > 0 ? static_cast<float>(*it - *p_out_min_value) / value_range : 1.0);
  }
}

void terminal_output::draw_cpu_usage_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  const auto& latest_diff = diff_queue_.back();
  draw_window_border(p_win, std::format("CPU Usage @ Freq {:.2f}mhz Temp {:.2f}C", latest_diff.cpu_freq.avg,
                                        latest_diff.cpu_temperature.avg));

  std::vector<float> values;
  for (const auto& diff : diff_queue_) {
    values.push_back(diff.cpu_usage);
  }
  draw_bar_chart(p_win, "100%", "0%", values);
}

void terminal_output::draw_cpu_freq_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  const auto& latest_diff = diff_queue_.back();
  draw_window_border(p_win, std::format("CPU Freq (mhz) @ Usage {:.2f}% Temp {:.2f}C", latest_diff.cpu_usage * 100,
                                        latest_diff.gpu_temperature.avg));

  float min_freq = 0, max_freq = 0;
  for (const auto& diff : diff_queue_) {
    if (max_freq == 0 || diff.cpu_freq.avg > max_freq) {
      max_freq = diff.cpu_freq.avg;
    }
    if (min_freq == 0 || diff.cpu_freq.avg < min_freq) {
      min_freq = diff.cpu_freq.avg;
    }
  }
  float freq_range = max_freq - min_freq;
  std::vector<float> values;
  for (const auto& diff : diff_queue_) {
    values.push_back(freq_range > 0 ? (diff.cpu_freq.avg - min_freq) / freq_range : 1.0);
  }

  draw_bar_chart(p_win, std::format("{:.2f}", max_freq), std::format("{:.2f}", min_freq), values);
}

void terminal_output::draw_gpu_freq_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  const auto& latest_diff = diff_queue_.back();
  draw_window_border(p_win, std::format("GPU Freq (mhz) @ Temp {:.2f}C", latest_diff.gpu_temperature.avg));

  float min_freq = 0, max_freq = 0;
  for (const auto& diff : diff_queue_) {
    if (max_freq == 0 || diff.gpu_freq > max_freq) {
      max_freq = diff.gpu_freq;
    }
    if (min_freq == 0 || diff.gpu_freq < min_freq) {
      min_freq = diff.gpu_freq;
    }
  }
  float freq_range = max_freq - min_freq;
  std::vector<float> values;
  for (const auto& diff : diff_queue_) {
    values.push_back(freq_range > 0 ? (diff.gpu_freq - min_freq) / freq_range : 1.0);
  }

  draw_bar_chart(p_win, std::format("{:.2f}", max_freq), std::format("{:.2f}", min_freq), values);
}

void terminal_output::draw_memory_usage_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  const auto& latest_diff = diff_queue_.back();
  draw_window_border(p_win, std::format("Memory Usage @ Temp {:.2f}C", latest_diff.memory_temperature.avg));

  std::vector<float> values;
  for (const auto& diff : diff_queue_) {
    values.push_back(diff.memory_usage);
  }

  draw_bar_chart(p_win, "100%", "0%", values);
}

void terminal_output::draw_disk_io_read_rate_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Total Disk I/O Read Rate (kb/s)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view =
      diff_queue_ | std::views::transform([](auto&& diff) { return diff.total_disk_io_rate.read_bytes / 1000.0; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_disk_io_write_rate_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Total Disk I/O Write Rate (kb/s)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view =
      diff_queue_ | std::views::transform([](auto&& diff) { return diff.total_disk_io_rate.write_bytes / 1000.0; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_network_io_read_rate_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Total Network I/O Read Rate (kb/s)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view =
      diff_queue_ | std::views::transform([](auto&& diff) { return diff.total_net_io_rate.read_bytes / 1000.0; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_network_io_write_rate_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Total Network I/O Write Rate (kb/s)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view =
      diff_queue_ | std::views::transform([](auto&& diff) { return diff.total_net_io_rate.write_bytes / 1000.0; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_cpu_temperature_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "CPU Temperature (C)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.cpu_temperature.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_gpu_temperature_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "GPU Temperature (C)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.gpu_temperature.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_memory_temperature_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Memory Temperature (C)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.memory_temperature.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_disk_temperature_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Disk Temperature (C)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.disk_temperature.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_battery_temperature_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Battery Temperature (C)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.battery_temperature.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

void terminal_output::draw_fan_speed_window(WINDOW* p_win) {
  if (!p_win || diff_queue_.size() == 0) {
    return;
  }
  wclear(p_win);

  draw_window_border(p_win, "Fan Speed (RPM)");
  std::vector<float> values;
  float min_value = 0, max_value = 0;
  const auto view = diff_queue_ | std::views::transform([](auto&& diff) { return diff.fan_speed.avg; });
  build_timeline_data(view.begin(), view.end(), &values, &max_value, &min_value);
  draw_bar_chart(p_win, std::format("{:.2f}", max_value), std::format("{:.2f}", min_value), values);
}

auto terminal_output::get_display_type() -> display_type {
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);
  if (max_x >= 120 && max_y >= 21) {
    return display_type::large;
  } else if (max_x >= 60 && max_y >= 21) {
    return display_type::middle;
  }
  return display_type::tiny;
}

#endif

// ------ Implement text output

void text_output::run() {
  size_t current_number = 0;
  while (!stop) {
    get_sampler().sample([&](sample_diff&& diff) {
      ++current_number;
      auto flags = std::cout.flags();
      std::cout << std::fixed << std::setprecision(6);
      std::cout << "========== Report " << std::format("{0:%F}T{0:%T}", diff.start_time) << " - "
                << std::format("{0:%F}T{0:%T}", diff.end_time) << " " << diff.sample_num << " samples in "
                << std::chrono::duration_cast<std::chrono::milliseconds>(diff.sample_duration).count()
                << "ms ==========\n";
      // Temperature
      std::cout << "Temperature (in celsius)\n";
      for (const auto& m : diff.temperatures) {
        std::cout << '\t' << describe::enum_to_string(m.device, "unknown") << '\t' << m.num << '\t' << m.value << '\n';
      }
      std::cout << "\tstats.cpu\tavg=" << diff.cpu_temperature.avg << "\tmax=" << diff.cpu_temperature.max
                << "\tmin=" << diff.cpu_temperature.min << '\n';
      std::cout << "\tstats.gpu\tavg=" << diff.gpu_temperature.avg << "\tmax=" << diff.gpu_temperature.max
                << "\tmin=" << diff.gpu_temperature.min << '\n';
      std::cout << "\tstats.memory\tavg=" << diff.memory_temperature.avg << "\tmax=" << diff.memory_temperature.max
                << "\tmin=" << diff.memory_temperature.min << '\n';
      std::cout << "\tstats.disk\tavg=" << diff.disk_temperature.avg << "\tmax=" << diff.disk_temperature.max
                << "\tmin=" << diff.disk_temperature.min << '\n';
      std::cout << "\tstats.battery\tavg=" << diff.battery_temperature.avg << "\tmax=" << diff.battery_temperature.max
                << "\tmin=" << diff.battery_temperature.min << '\n';
      // Fan speeds
      std::cout << "Fan speed (in rpm)\n";
      for (const auto& m : diff.fan_speeds) {
        std::cout << '\t' << m.num << '\t' << m.value << '\n';
      }
      std::cout << "\tstats\tavg=" << diff.fan_speed.avg << "\tmax=" << diff.fan_speed.max
                << "\tmin=" << diff.fan_speed.min << '\n';
      // Power consumptions
      std::cout << "Power consumption (in watt)\n";
      for (const auto& [t, v] : diff.power_consumptions) {
        std::cout << '\t' << describe::enum_to_string(t, "unknown") << '\t' << v << '\n';
      }
      for (const auto& m : diff.cpu_core_power_consumptions) {
        std::cout << "\tcpu_core\t" << describe::enum_to_string(m.device, "unknown") << '\t' << m.num << '\t' << m.value
                  << '\n';
      }
      // Cpu
      std::cout << "Cpu usage (%)\n\tusage\t" << diff.cpu_usage << '\n';
      for (const auto& m : diff.cpu_core_usage) {
        std::cout << "\tcpu_core_usage\t" << describe::enum_to_string(m.device, "unknown") << '\t' << m.num << '\t'
                  << m.value << '\n';
      }
      std::cout << "Cpu freq (in mhz)\n";
      std::cout << "\tcpu\tavg=" << diff.cpu_freq.avg << "\tmax=" << diff.cpu_freq.max << "\tmin=" << diff.cpu_freq.min
                << '\n';
      for (const auto& [t, m] : diff.cpu_core_type_freq) {
        std::cout << "\tcpu_core_type\t" << describe::enum_to_string(t, "unknown") << "\tavg=" << m.avg
                  << "\tmax=" << m.max << "\tmin=" << m.min << '\n';
      }
      for (const auto& m : diff.cpu_core_freqs) {
        std::cout << "\tcpu_core\t" << describe::enum_to_string(m.device, "unknown") << '\t' << m.num << '\t' << m.value
                  << '\n';
      }
      // Gpu
      std::cout << "Gpu freq (in mhz)\n\tgpu\t" << diff.gpu_freq << '\n';
      // Memory
      std::cout << "Memory\n\ttotal\t" << diff.memory_size << "\n\tusage\t" << diff.memory_usage << "\n\tavailable\t"
                << diff.memory_available_percentage << "\n\tfree\t" << diff.memory_free_percentage << '\n';
      // Disk io
      std::cout << "Disk io counter (bytes)\n\ttotal\tread_opts\t" << diff.total_disk_io_counter.read_opts
                << "\n\ttotal\twrite_opts\t" << diff.total_disk_io_counter.write_opts << "\n\ttotal\tread_bytes\t"
                << diff.total_disk_io_counter.read_bytes << "\n\ttotal\twrite_bytes\t"
                << diff.total_disk_io_counter.write_bytes << '\n';
      for (const auto& [name, counter] : diff.disk_io_counter_per_disk) {
        std::cout << "\t" << name << "\tread_opts\t" << counter.read_opts << "\n\t" << name << "\twrite_opts\t"
                  << counter.write_opts << "\n\t" << name << "\tread_bytes\t" << counter.read_bytes << "\n\t" << name
                  << "\twrite_bytes\t" << counter.write_bytes << '\n';
      }
      std::cout << "Disk io rate (bytes/s)\n\ttotal\tread_opts\t" << diff.total_disk_io_rate.read_opts
                << "\n\ttotal\twrite_opts\t" << diff.total_disk_io_rate.write_opts << "\n\ttotal\tread_bytes\t"
                << diff.total_disk_io_rate.read_bytes << "\n\ttotal\twrite_bytes\t"
                << diff.total_disk_io_rate.write_bytes << '\n';
      for (const auto& [name, rate] : diff.disk_io_rate_per_disk) {
        std::cout << "\t" << name << "\tread_opts\t" << rate.read_opts << "\n\t" << name << "\twrite_opts\t"
                  << rate.write_opts << "\n\t" << name << "\tread_bytes\t" << rate.read_bytes << "\n\t" << name
                  << "\twrite_bytes\t" << rate.write_bytes << '\n';
      }
      // Network io
      std::cout << "Network io counter (bytes)\n\ttotal\tread_bytes\t" << diff.total_net_io_counter.read_bytes
                << "\n\ttotal\twrite_bytes\t" << diff.total_net_io_counter.write_bytes << "\n\ttotal\tread_packets\t"
                << diff.total_net_io_counter.read_packets << "\n\ttotal\twrite_packets\t"
                << diff.total_net_io_counter.write_packets << '\n';
      for (const auto& [name, counter] : diff.net_io_counter_per_if) {
        std::cout << "\t" << name << "\tread_bytes\t" << counter.read_bytes << "\n\t" << name << "\twrite_bytes\t"
                  << counter.write_bytes << "\n\t" << name << "\tread_packets\t" << counter.read_packets << "\n\t"
                  << name << "\twrite_packets\t" << counter.write_packets << '\n';
      }
      std::cout << "Network io rate (bytes/s)\n\ttotal\tread_bytes\t" << diff.total_net_io_rate.read_bytes
                << "\n\ttotal\twrite_bytes\t" << diff.total_net_io_rate.write_bytes << "\n\ttotal\tread_packets\t"
                << diff.total_net_io_rate.read_packets << "\n\ttotal\twrite_packets\t"
                << diff.total_net_io_rate.write_packets << '\n';
      for (const auto& [name, rate] : diff.net_io_rate_per_if) {
        std::cout << "\t" << name << "\tread_bytes\t" << rate.read_bytes << "\n\t" << name << "\twrite_bytes\t"
                  << rate.write_bytes << "\n\t" << name << "\tread_packets\t" << rate.read_packets << "\n\t" << name
                  << "\twrite_packets\t" << rate.write_packets << '\n';
      }
      // Errors
      if (diff.errors.size() > 0) {
        std::cout << "Errors\n";
        for (const auto& ec : diff.errors) {
          std::cout << '\t' << ec.value() << '\t' << ec.message() << '\n';
        }
      }
      std::cout << std::flush;
      // Restore flags
      std::cout.flags(flags);
    });
    if (stop || (output_number > 0 && current_number >= output_number)) {
      break;
    }
    std::this_thread::sleep_for(1s);
  }
}

// ------ Implement json output

namespace std {

void tag_invoke(const json::value_from_tag&, json::value& jv, const error_code& ec) {
  jv = {
      {"code", ec.value()},
      {"message", ec.message()},
  };
}

namespace chrono {

void tag_invoke(const json::value_from_tag&, json::value& jv, const system_clock::time_point& t) {
  jv = std::format("{0:%F} {0:%T}", t);
}

void tag_invoke(const json::value_from_tag&, json::value& jv, const system_clock::duration& d) {
  jv = duration_cast<milliseconds>(d).count();
}

}  // namespace chrono
}  // namespace std

void json_output::run() {
  size_t current_number = 0;
  while (!stop) {
    get_sampler().sample([&](sample_diff&& diff) {
      ++current_number;
      // Write one json string per line
      std::cout << boost::json::value_from(diff) << std::endl;
    });
    if (stop || (output_number > 0 && current_number >= output_number)) {
      break;
    }
    std::this_thread::sleep_for(1s);
  }
}

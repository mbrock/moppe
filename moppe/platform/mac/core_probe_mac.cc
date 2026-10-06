// The core probe (moppe/game/core_probe.hh) on macOS, from the command line,
// for comparison with the consoles that run it before they have a renderer:
//
//   moppe-core-probe [--seed N] [--resolution N] [--keep-caches] [REPORT]
//
// It runs from the repository root (for fonts/ and textures/), or with
// MOPPE_ASSETS set, and writes the report to REPORT (default
// core-probe-report.txt) as it grows.

#include <moppe/game/core_probe.hh>
#include <moppe/platform/platform.hh>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>

namespace moppe::platform {
  // The windowed host's half of platform.hh, which a headless run has no
  // use for; apple_common.mm supplies the rest.
  void request_quit () {}
  void set_window_title (const std::string&) {}
  void set_pointer_captured (bool) {}
  Insets safe_insets () {
    return {};
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  game::CoreProbeOptions options;
  std::string report_path = "core-probe-report.txt";
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument = argv[i];
    if (argument == "--seed" && i + 1 < argc)
      options.seed = std::atoi (argv[++i]);
    else if (argument == "--resolution" && i + 1 < argc)
      options.resolution = std::atoi (argv[++i]);
    else if (argument == "--keep-caches")
      options.clear_caches = false;
    else
      report_path = argument;
  }

  game::CoreProbeHost host;
  host.platform_name = "macOS";
  // platform::async completes on the main dispatch queue, which the main
  // run loop drains.
  host.pump = [] { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.005, true); };
  host.memory = [] {
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    game::CoreProbeMemory reading;
    if (task_info (mach_task_self (),
                   TASK_VM_INFO,
                   reinterpret_cast<task_info_t> (&info),
                   &count) == KERN_SUCCESS) {
      reading.current = info.phys_footprint;
      reading.peak = info.ledger_phys_footprint_peak;
    }
    return reading;
  };
  host.write_report = [&report_path] (const std::string& text) {
    std::ofstream (report_path) << text;
  };
  std::string last_title;
  host.progress = [&last_title] (const std::string& what, float) {
    const std::string title = what.substr (0, what.find ('\n'));
    if (title != last_title)
      std::cerr << "core probe: " << title << std::endl;
    last_title = title;
  };
  const bool ok = game::run_core_probe (host, options);
  std::ifstream report (report_path);
  std::cout << report.rdbuf ();
  return ok ? 0 : 1;
}

// The game on Xbox Series consoles in Developer Mode: SDL3's UWP backend
// (nixbox's fork) runs the CoreApplication, and the SDL host draws into its
// CoreWindow through Direct3D 12 (moppe/platform/sdl/device_d3d12.cc).
// This file is the console's prelude to main.cc's main, compiled for this
// target as moppe_main:
//
//   - everything the game writes to std::cerr goes to LocalState/log.txt;
//   - environment.txt in the package, then in LocalState, holds NAME=VALUE
//     lines for moppe::environment, MOPPE_ARGS there being the command line;
//   - LocalState/control.txt is the remote-control file (input.hh), which
//     tools/xbox-control uploads through Device Portal;
//   - the frame rate and each pass's GPU time are logged, and a watchdog
//     names the device's step when frames stop advancing.

#include <moppe/environment.hh>
#include <moppe/nhal/d3d12/d3d12_device.hh>
#include <moppe/platform/platform.hh>
#include <moppe/platform/uwp/uwp.hh>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_main.h>

int moppe_main (int argc, char** argv);

namespace {
  std::vector<std::string> split (const std::string& line) {
    std::vector<std::string> words;
    std::istringstream input (line);
    for (std::string word; input >> word;)
      words.push_back (word);
    return words;
  }

  // Names the device's step in the log when frames stop advancing.
  void watch_frames () {
    std::thread ([] {
      long seen = -1;
      for (;;) {
        std::this_thread::sleep_for (std::chrono::seconds (3));
        const long now = moppe::platform::uwp::frames_rendered ();
        if (now == seen)
          std::cerr << "moppe-xbox: stalled at frame " << now << ": "
                    << moppe::nhal::d3d12_device_step () << std::endl;
        seen = now;
      }
    }).detach ();
  }
}

int main (int, char**) {
  namespace uwp = moppe::platform::uwp;
  const std::string local = uwp::local_state_path ();
  static std::ofstream log (local + "log.txt", std::ios::trunc);
  if (log)
    std::cerr.rdbuf (log.rdbuf ());
  const int packaged = uwp::load_environment_file (
    moppe::platform::asset_path ("environment.txt"));
  const int local_variables =
    uwp::load_environment_file (local + "environment.txt");
  // A development console reports its frame rate and where its GPU time
  // goes, and listens for the remote.
  for (const char* name : { "MOPPE_NHAL_TIMINGS", "MOPPE_FPS_REPORT" })
    if (!moppe::environment (name))
      moppe::set_environment (name, "1");
  if (!moppe::environment ("MOPPE_CONTROL_FILE"))
    moppe::set_environment ("MOPPE_CONTROL_FILE",
                            (local + "control.txt").c_str ());
  std::cerr << "moppe-xbox: " << packaged << " packaged and "
            << local_variables << " local environment variables" << std::endl;
  watch_frames ();

  std::vector<std::string> words { "moppe" };
  if (const char* arguments = moppe::environment ("MOPPE_ARGS"))
    for (std::string& word : split (arguments))
      words.push_back (std::move (word));
  std::vector<char*> argv;
  for (std::string& word : words)
    argv.push_back (word.data ());
  argv.push_back (nullptr);
  const int status = moppe_main (int (words.size ()), argv.data ());
  std::cerr << "moppe-xbox: exit " << status << std::endl;
  return status;
}

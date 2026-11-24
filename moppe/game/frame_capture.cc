#include <moppe/environment.hh>
#include <moppe/game/frame_capture.hh>
#include <moppe/game/opening.hh>
#include <moppe/platform/platform.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

namespace moppe::game {
  FrameCapture::FrameCapture (std::string screenshot_path)
      : m_screenshot_path (std::move (screenshot_path)) {}

  int FrameCapture::video_steps (bool driven) const {
    if (!m_video_plan || m_video_done || driven)
      return 1;
    const int first = static_cast<int> (
      std::lround (seconds_value (m_video_plan->start) * 60.0f));
    return std::clamp (first - 120 - m_video_render_frame, 1, 20);
  }

  std::optional<platform::host_time_t>
  FrameCapture::script_seconds (bool playing) const {
    if (!m_video_plan)
      return std::nullopt;
    return platform::host_time (playing ? m_video_render_frame / 60.0 : -1.0);
  }

  void FrameCapture::toggle_clip () {
    if (m_video.recording ())
      m_video.finish ();
    else if (!m_video_plan)
      m_video.begin (next_clip_path (), 30);
  }

  void FrameCapture::finish_video () {
    m_video.finish ();
  }

  bool FrameCapture::before_frame (render::Renderer& r,
                                   const FrameView& frame,
                                   seconds_t play_time,
                                   proportion_t sun_height,
                                   bool opening_active) {
    static const int screenshot_delay = [] {
      if (const char* frames = moppe::environment ("MOPPE_SCREENSHOT_FRAMES"))
        return std::max (1, ::atoi (frames));
      return 30;
    }();
    const bool captured =
      !m_screenshot_path.empty () && ++m_screenshot_frames >= screenshot_delay;
    if (captured)
      r.request_screenshot (m_screenshot_path);
    if (m_snapshot_requested) {
      m_snapshot_requested = false;
      const std::string path = next_snapshot_path ();
      r.request_screenshot (path);
      record_shot_pose (frame, path, play_time, sun_height);
    }
    record_frame (r, opening_active);
    return captured;
  }

  void FrameCapture::after_frame (bool still_taken) {
    if (!still_taken)
      return;
    m_screenshot_path.clear ();
    platform::request_quit ();
  }

  std::optional<FrameCapture::VideoPlan> FrameCapture::plan_video () {
    const char* path = moppe::environment ("MOPPE_VIDEO");
    if (!path || !*path)
      return std::nullopt;
    VideoPlan plan;
    plan.path = path;
    const auto number = [] (const char* name, float otherwise) {
      const char* value = moppe::environment (name);
      return value ? (float)::atof (value) : otherwise;
    };
    plan.fps = number ("MOPPE_VIDEO_FPS", 30.0f) > 45.0f ? 60 : 30;
    plan.seconds =
      seconds (std::max (0.0f, number ("MOPPE_VIDEO_SECONDS", 0.0f)));
    plan.start = seconds (std::max (0.0f, number ("MOPPE_VIDEO_START", 0.0f)));
    plan.speed =
      std::max (1, static_cast<int> (number ("MOPPE_VIDEO_SPEED", 1.0f)));
    plan.height =
      std::max (0, static_cast<int> (number ("MOPPE_VIDEO_HEIGHT", 0.0f)));
    return plan;
  }

  void FrameCapture::record_frame (render::Renderer& r, bool opening_active) {
    const auto take = [&] {
      r.request_frame (
        [this] (const render::FramePixels& frame) { m_video.add (frame); });
    };
    const int index = m_video_render_frame++;
    if (!m_video_plan) {
      // A video begun with V takes every second frame as they come.
      if (m_video.recording () && index % 2 == 0)
        take ();
      return;
    }
    if (m_video_done)
      return;
    const VideoPlan& plan = *m_video_plan;
    const int first =
      static_cast<int> (std::lround (seconds_value (plan.start) * 60.0f));
    if (index < first)
      return;
    if (index == first)
      m_video.begin (plan.path, plan.fps, plan.height);
    // A video of a drone flight (`flight` or `river`) is over when the flight
    // lands: what follows is only the walker standing at the trailhead.
    static const bool flight_only = [] {
      const char* opening = moppe::environment ("MOPPE_OPENING");
      return opening && (std::string_view (opening) == "river" ||
                         std::string_view (opening) == "flight");
    }();
    if (flight_only && opening_active)
      m_video_saw_flight = true;
    const bool landed = flight_only && m_video_saw_flight && !opening_active;
    if (!m_video.recording () || landed ||
        (plan.seconds > seconds (0.0f) &&
         m_video.frames () >=
           std::lround (seconds_value (plan.seconds) * plan.fps))) {
      m_video.finish ();
      m_video_done = true;
      platform::request_quit ();
      return;
    }
    if ((index - first) % (60 / plan.fps * plan.speed) == 0)
      take ();
  }

  std::string FrameCapture::next_clip_path () {
    std::ostringstream path;
    path << snapshot_directory () << "/clip-" << std::setfill ('0')
         << std::setw (3) << m_clip_count++ << ".mp4";
    return path.str ();
  }

  const std::string& FrameCapture::snapshot_directory () {
    namespace fs = std::filesystem;
    if (m_snapshot_directory.empty ()) {
      char stamp[32];
      const std::time_t now = std::time (nullptr);
      std::strftime (
        stamp, sizeof stamp, "run-%Y%m%d-%H%M%S", std::localtime (&now));
      fs::path directory = fs::path ("screenshots") / stamp;
      std::error_code error;
      fs::create_directories (directory, error);
      if (error) {
        directory = fs::temp_directory_path () / "moppe-screenshots" / stamp;
        fs::create_directories (directory, error);
      }
      m_snapshot_directory = directory.string ();
    }
    return m_snapshot_directory;
  }

  std::string FrameCapture::next_snapshot_path () {
    std::ostringstream path;
    path << snapshot_directory () << "/shot-" << std::setfill ('0')
         << std::setw (3) << m_snapshot_count++ << ".png";
    std::cerr << "moppe: screenshot " << path.str () << '\n';
    return path.str ();
  }

  void FrameCapture::record_shot_pose (const FrameView& frame,
                                       const std::string& path,
                                       seconds_t play_time,
                                       proportion_t sun_height) {
    const std::string name = std::filesystem::path (path).stem ().string ();
    const std::string line =
      format_opening_shot (name,
                           position_value (frame.camera.position),
                           direction_value (frame.camera.forward),
                           frame.camera.field_of_view,
                           play_time,
                           sun_height);
    std::cerr << "moppe: pose: " << line << '\n';
    std::ofstream shots (std::filesystem::path (m_snapshot_directory) /
                           "shots.txt",
                         std::ios::app);
    shots << line << '\n';
  }
}

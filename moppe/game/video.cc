#include <moppe/game/video.hh>

#include <moppe/environment.hh>

#include <iostream>
#include <sstream>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Starting an encoder needs a shell and a pipe: the desktops have them.
#if (defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX)) &&           \
  !defined(__EMSCRIPTEN__)
#define MOPPE_VIDEO_PIPE 1
#include <csignal>
#include <cstdlib>
#endif

namespace moppe::game {
  namespace video_parts {
    // A word the shell reads as itself.
    std::string quoted (const std::string& word) {
      std::string out = "'";
      for (const char c : word)
        out += c == '\'' ? std::string ("'\\''") : std::string (1, c);
      return out + "'";
    }
  }

  VideoRecorder::~VideoRecorder () {
    finish ();
  }

  void VideoRecorder::begin (std::string path, int fps, int height) {
    finish ();
    m_path = std::move (path);
    m_fps = fps;
    m_scaled_height = height;
    m_frames = 0;
#ifdef MOPPE_VIDEO_PIPE
    m_recording = true;
#else
    std::cerr << "moppe: video: this platform cannot record\n";
#endif
  }

  void VideoRecorder::add (const render::FramePixels& frame) {
    if (!m_recording)
      return;
#ifdef MOPPE_VIDEO_PIPE
    if (!m_pipe) {
      const char* named = moppe::environment ("MOPPE_FFMPEG");
      const std::string ffmpeg = video_parts::quoted (named ? named : "ffmpeg");
      if (std::system (
            ("command -v " + ffmpeg + " >/dev/null 2>&1").c_str ())) {
        std::cerr << "moppe: video: no ffmpeg on PATH (the development shell "
                     "has one; MOPPE_FFMPEG names another)\n";
        m_recording = false;
        return;
      }
      // An encoder that dies must not take the game with it.
      std::signal (SIGPIPE, SIG_IGN);
      m_width = frame.width;
      m_height = frame.height;
      // H.264's 4:2:0 wants even dimensions.
      std::ostringstream filter;
      if (m_scaled_height > 0)
        filter << "scale=-2:" << m_scaled_height / 2 * 2 << ":flags=lanczos";
      else
        filter << "pad=ceil(iw/2)*2:ceil(ih/2)*2";
      std::ostringstream command;
      command << ffmpeg << " -hide_banner -loglevel error -y"
              << " -f rawvideo -pix_fmt rgb24 -video_size " << m_width << 'x'
              << m_height << " -framerate " << m_fps << " -i -"
              << " -vf " << video_parts::quoted (filter.str ())
              << " -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p"
              << " -movflags +faststart " << video_parts::quoted (m_path);
      m_pipe = ::popen (command.str ().c_str (), "w");
      if (!m_pipe) {
        std::cerr << "moppe: video: could not start ffmpeg\n";
        m_recording = false;
        return;
      }
      std::cerr << "moppe: video: recording " << m_width << 'x' << m_height
                << " at " << m_fps << " fps to " << m_path << std::endl;
    }
    if (frame.width != m_width || frame.height != m_height) {
      std::cerr << "moppe: video: the frame changed size\n";
      finish ();
      return;
    }
    if (std::fwrite (frame.rgb.data (), 1, frame.rgb.size (), m_pipe) !=
        frame.rgb.size ()) {
      std::cerr << "moppe: video: ffmpeg stopped taking frames\n";
      finish ();
      return;
    }
    ++m_frames;
#else
    (void)frame;
#endif
  }

  void VideoRecorder::finish () {
#ifdef MOPPE_VIDEO_PIPE
    if (m_pipe) {
      const int status = ::pclose (m_pipe);
      m_pipe = nullptr;
      if (status == 0)
        std::cerr << "moppe: video: wrote " << m_path << ", " << m_frames
                  << " frames, " << static_cast<float> (m_frames) / m_fps
                  << " s" << std::endl;
      else
        std::cerr << "moppe: video: ffmpeg failed writing " << m_path
                  << std::endl;
    }
#endif
    m_recording = false;
  }
}

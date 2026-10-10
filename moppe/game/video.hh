#ifndef MOPPE_GAME_VIDEO_HH
#define MOPPE_GAME_VIDEO_HH

// Recording the game to a video file: finished frames
// (render::Renderer::request_frame) go as raw pixels down a pipe to an
// ffmpeg that encodes H.264 into an MP4. ffmpeg is the one on PATH -- the
// development shell has it -- or the one MOPPE_FFMPEG names; without one,
// or on a platform that cannot start a program, nothing is recorded.

#include <moppe/render/renderer.hh>

#include <cstdio>
#include <string>

namespace moppe::game {
  class VideoRecorder {
  public:
    VideoRecorder () = default;
    VideoRecorder (const VideoRecorder&) = delete;
    VideoRecorder& operator= (const VideoRecorder&) = delete;
    ~VideoRecorder ();

    // Begins a recording to `path` at `fps` frames a second, scaled to
    // `height` pixels high when that is not zero. The encoder starts with
    // the first frame, whose size it takes.
    void begin (std::string path, int fps, int height = 0);
    // One more frame of it; a frame of another size than the first ends
    // the recording.
    void add (const render::FramePixels& frame);
    // Ends the recording and waits for the file to be complete.
    void finish ();

    bool recording () const {
      return m_recording;
    }
    int frames () const {
      return m_frames;
    }
    const std::string& path () const {
      return m_path;
    }

  private:
    std::string m_path;
    int m_fps = 30;
    int m_scaled_height = 0;
    bool m_recording = false;
    std::FILE* m_pipe = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_frames = 0;
  };
}

#endif

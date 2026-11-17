// What a launch takes of its own frames: the still named on the command
// line (--screenshot), the screenshot key's numbered series and the poses
// beside it, the video key's clips, and a video recorded from start to
// end (MOPPE_VIDEO, tools/record), which also sets how the world steps.
#ifndef MOPPE_GAME_FRAME_CAPTURE_HH
#define MOPPE_GAME_FRAME_CAPTURE_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/video.hh>
#include <moppe/platform/platform.hh>
#include <moppe/render/renderer.hh>

#include <optional>
#include <string>

namespace moppe::game {
  class FrameCapture {
  public:
    explicit FrameCapture (std::string screenshot_path);

    // A launch told to record itself: MOPPE_VIDEO names the file, and
    // beside it MOPPE_VIDEO_SECONDS how long the video runs before the
    // game quits (otherwise until it is quit), MOPPE_VIDEO_START the
    // seconds of play to let pass first, MOPPE_VIDEO_FPS 30 or 60,
    // MOPPE_VIDEO_SPEED a whole number of times faster than life, and
    // MOPPE_VIDEO_HEIGHT a height to scale to. The world then advances a
    // sixtieth of a second per rendered frame, so the video is even
    // however long its frames took (tools/record).
    struct VideoPlan {
      std::string path;
      int fps = 30;
      float seconds = 0.0f;
      float start = 0.0f;
      int speed = 1;
      int height = 0;
    };

    // Whether the launch takes the still named on its command line.
    bool taking_still () const noexcept {
      return !m_screenshot_path.empty ();
    }

    // Whether the launch records a planned video.
    bool video_planned () const noexcept {
      return m_video_plan.has_value ();
    }

    // Captures and recordings draw each frame they are given however long
    // that takes; only play keeps pace with the display.
    bool keeping_pace () const noexcept {
      return !video_planned () && !taking_still ();
    }

    // How many 1/60 s steps of the world this drawn frame stands for in a
    // planned video: many while the time before it begins goes by
    // unseen, until two seconds before its first frame, and one after.
    // `driven`, a remote control's taps are timed by frames, so one each.
    int video_steps (bool driven) const;

    // Counts the frames a planned video's steps stood for beyond one.
    void skip_video_frames (int frames) noexcept {
      m_video_render_frame += frames;
    }

    // The moment of a planned video drawn so far, for its remote
    // control: its seconds after the host clock's epoch, and before the
    // epoch until play shows.
    std::optional<platform::host_time_t> script_seconds (bool playing) const;

    // The screenshot key: the next frame drawn is kept.
    void request_snapshot () noexcept {
      m_snapshot_requested = true;
    }

    // The video key: a clip starts, or the one recording ends.
    void toggle_clip ();

    // Ends any video being recorded, before quitting.
    void finish_video ();

    // Before a frame of play is begun: asks the renderer for the still
    // when its delay is up, for a requested snapshot (with its pose, at
    // `play_seconds` under `sun_height`), and for a video's frame.
    // Returns whether the still was asked for this frame.
    bool before_frame (render::Renderer& r,
                       const FrameView& frame,
                       float play_seconds,
                       float sun_height,
                       bool opening_active);

    // After the frame: a launch whose still is taken quits.
    void after_frame (bool still_taken);

  private:
    static std::optional<VideoPlan> plan_video ();

    // Hands this frame to the video being recorded, if it is one of the
    // frames the video takes.
    void record_frame (render::Renderer& r, bool opening_active);

    std::string next_clip_path ();

    // The screenshot key and the video key drop their files into one
    // per-run timestamped directory.
    const std::string& snapshot_directory ();

    // The in-game screenshot key's frames number on through a run, so a
    // walk through the world becomes a reviewable series.
    std::string next_snapshot_path ();

    // Beside each screenshot, the camera that took it as a `shot` line for
    // data/opening.txt: printed to the log (on the Xbox, LocalState's
    // log.txt) and gathered in the run directory's shots.txt.
    void record_shot_pose (const FrameView& frame,
                           const std::string& path,
                           float play_seconds,
                           float sun_height);

    std::string m_screenshot_path;
    int m_screenshot_frames = 0;
    bool m_snapshot_requested = false;
    std::string m_snapshot_directory;
    int m_snapshot_count = 0;
    VideoRecorder m_video;
    std::optional<VideoPlan> m_video_plan = plan_video ();
    int m_video_render_frame = 0;
    bool m_video_saw_flight = false;
    int m_clip_count = 0;
    bool m_video_done = false;
  };
}

#endif

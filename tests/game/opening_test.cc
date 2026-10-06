#include <moppe/game/opening.hh>

#include <tests/test.hh>

#include <fstream>
#include <sstream>

using namespace moppe;
using namespace moppe::game;

namespace {
  std::optional<OpeningReel> parse (const std::string& text,
                                    std::string& error) {
    std::istringstream input (text);
    return parse_opening_reel (input, error);
  }
}

MOPPE_TEST (opening_reel_parses_shots_captions_and_arrival) {
  std::string error;
  const std::optional<OpeningReel> reel =
    parse ("# a comment\n"
           "world seed 123 resolution 2048 profile play\n"
           "shot lake eye 10 20 30 heading 90 pitch -2 fov 48 hold 8 "
           "push 3 pan -4 fade 2.5 clock 400  # trailing comment\n"
           "caption title \"moppe\" at 2 for 4\n"
           "shot meadow eye 1 2 3 heading 0 pitch 0 fov 60 sway 0\n"
           "caption credit \"a game by Mikael Brockman\"\n"
           "arrival hold 5 push 4 rise 1\n",
           error);
  MOPPE_CHECK (reel.has_value ());
  MOPPE_CHECK (reel->matches (123, 2048, "play"));
  MOPPE_CHECK (!reel->matches (124, 2048, "play"));
  MOPPE_CHECK (!reel->matches (123, 1024, "play"));
  MOPPE_CHECK (reel->shots.size () == 2);
  const OpeningShot& lake = reel->shots[0];
  MOPPE_CHECK (lake.name == "lake");
  MOPPE_CHECK_NEAR (lake.eye[1], 20.0f, 1e-6f);
  MOPPE_CHECK_NEAR (lake.heading_deg, 90.0f, 1e-6f);
  MOPPE_CHECK_NEAR (lake.fov_deg, 48.0f, 1e-6f);
  MOPPE_CHECK_NEAR (lake.push, 3.0f, 1e-6f);
  MOPPE_CHECK_NEAR (lake.fade, 2.5f, 1e-6f);
  MOPPE_CHECK (lake.clock && *lake.clock == 400.0f);
  MOPPE_CHECK (lake.captions.size () == 1);
  MOPPE_CHECK (lake.captions[0].text == "moppe");
  MOPPE_CHECK (reel->shots[1].captions[0].style ==
               OpeningCaption::Style::Credit);
  MOPPE_CHECK (reel->arrival.has_value ());
  MOPPE_CHECK_NEAR (reel->arrival->push, 4.0f, 1e-6f);
}

MOPPE_TEST (opening_reel_reports_the_line_of_a_mistake) {
  std::string error;
  MOPPE_CHECK (!parse ("shot a eye 1 2 3\nshot b eye 1 2\n", error));
  MOPPE_CHECK (error.find ("line 2") != std::string::npos);
  MOPPE_CHECK (!parse ("caption title \"orphan\"\n", error));
  MOPPE_CHECK (!parse ("shot a eye 1 2 3 wobble 4\n", error));
  MOPPE_CHECK (!parse ("shot a heading 4\n", error));
}

// The line P prints is a shot the parser reads back to the same camera.
MOPPE_TEST (opening_pose_line_round_trips) {
  const Vec3 eye (4454.04f, 153.9f, 3617.93f);
  const Vec3 forward = normalized (Vec3 (-0.9f, -0.05f, 0.2f));
  const std::string line =
    format_opening_shot ("shot-004", eye, forward, 70.0f, 412.0f, 0.62f);
  std::string error;
  const std::optional<OpeningReel> reel = parse (line + "\n", error);
  MOPPE_CHECK (reel.has_value ());
  const OpeningShot& shot = reel->shots.at (0);
  MOPPE_CHECK (shot.name == "shot-004");
  MOPPE_CHECK_NEAR (shot.eye[0], eye[0], 0.01f);
  MOPPE_CHECK_NEAR (shot.fov_deg, 70.0f, 0.05f);
  const Vec3 back = forward_from (shot.heading_deg, shot.pitch_deg);
  MOPPE_CHECK (length (back - forward) < 0.003f);
}

MOPPE_TEST (opening_player_cuts_fades_and_lands_in_the_arrival) {
  OpeningShot first;
  first.name = "first";
  first.eye = Vec3 (0, 10, 0);
  first.hold = 4.0f;
  first.fade = 2.0f;
  first.sway = 0.0f;
  first.push = 2.0f;
  first.captions.push_back (
    { OpeningCaption::Style::Title, "moppe", 1.0f, 2.0f });
  OpeningArrival arrival;
  arrival.hold = 3.0f;
  arrival.fade = 1.0f;
  const Vec3 eye (5, 2, 5);
  const OpeningShot last = arrival_shot (arrival, eye, Vec3 (1, 0, 0), 70.0f);

  OpeningPlayer player;
  player.start ({ first, last });
  MOPPE_CHECK (player.active ());
  MOPPE_CHECK_NEAR (player.duration (), 7.0f, 1e-5f);
  MOPPE_CHECK_NEAR (player.veil (), 1.0f, 1e-5f); // rising out of black
  MOPPE_CHECK (player.captions ().empty ());

  player.tick (2.0f);
  MOPPE_CHECK_NEAR (player.veil (), 0.0f, 1e-5f);
  MOPPE_CHECK_NEAR (player.position ()[2], 1.0f, 1e-4f); // halfway pushed
  MOPPE_CHECK (player.captions ().size () == 1);
  MOPPE_CHECK_NEAR (player.captions ()[0].alpha, 1.0f, 0.05f);

  player.tick (1.8f); // just before the dip's middle
  MOPPE_CHECK (player.veil () > 0.5f);
  MOPPE_CHECK (player.shot_index () == 0);

  player.tick (0.3f);
  MOPPE_CHECK (player.shot_index () == 1);
  MOPPE_CHECK (player.veil () > 0.5f);

  player.tick (2.84f);
  MOPPE_CHECK (player.active ());
  MOPPE_CHECK (length (player.position () - eye) < 0.01f);
  player.tick (0.1f);
  MOPPE_CHECK (!player.active ());
  MOPPE_CHECK (length (player.position () - eye) < 1e-4f);
  MOPPE_CHECK (length (player.forward () - Vec3 (1, 0, 0)) < 1e-4f);
}

// The shot list shipped in data/ parses and names the default world.
MOPPE_TEST (shipped_opening_reel_parses) {
  std::ifstream input (MOPPE_TEST_SOURCE_DIR "/data/opening.txt");
  MOPPE_CHECK (input.good ());
  std::string error;
  const std::optional<OpeningReel> reel = parse_opening_reel (input, error);
  if (!reel)
    throw std::runtime_error (error);
  MOPPE_CHECK (reel->matches (123, 2048, "play"));
  MOPPE_CHECK (reel->shots.size () >= 3);
  MOPPE_CHECK (reel->arrival.has_value ());
}

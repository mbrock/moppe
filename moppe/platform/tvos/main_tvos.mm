// tvOS platform layer: UIKit + MTKView + GameController input.

#import <GameController/GameController.h>
#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

#include <moppe/platform/apple/game_controller.hh>
#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/metal/metal_evaluator.hh>
#include <moppe/terrain/stream_power_evolution.hh>

#include <iostream>
#include <memory>

using moppe::platform::AppleGameController;
using moppe::platform::Config;
using moppe::platform::Game;

static Game* g_game = nullptr;
static Config g_config;
static __weak UIWindow* g_window = nil;

// ------------------------------------------------------------------

@interface MoppeTVViewController : GCEventViewController <MTKViewDelegate>
@property (nonatomic, assign) moppe::render::Renderer* renderer;
- (void)connectGameController;
- (void)disconnectGameController;
@end

@implementation MoppeTVViewController {
  double m_last_time;
  std::unique_ptr<AppleGameController> m_controller;
}

- (BOOL)prefersStatusBarHidden {
  return YES;
}

- (void)connectGameController {
  if (g_game && !m_controller)
    m_controller = std::make_unique<AppleGameController> (*g_game);
}

- (void)disconnectGameController {
  if (m_controller)
    m_controller->disconnect ();
  m_controller.reset ();
}

- (void)drawInMTKView:(MTKView*)view {
  if (!g_game || !self.renderer)
    return;
  if (m_controller)
    m_controller->poll ();

  const double t = moppe::platform::now ();
  float dt = m_last_time > 0 ? (float)(t - m_last_time) : 1.0f / 60;
  m_last_time = t;
  if (dt > 0.05f)
    dt = 0.05f;
  if (dt > 0)
    g_game->tick (dt);
  g_game->render (*self.renderer);
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  if (g_game)
    g_game->resize ((int)view.bounds.size.width, (int)view.bounds.size.height);
}
@end

// ------------------------------------------------------------------

@interface MoppeTVAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@property (strong, nonatomic) MoppeTVViewController* viewController;
@end

@implementation MoppeTVAppDelegate

- (BOOL)application:(UIApplication*)application
  didFinishLaunchingWithOptions:(NSDictionary*)options {
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  g_window = self.window;

  MoppeTVViewController* vc = [[MoppeTVViewController alloc] init];
  vc.controllerUserInteractionEnabled = NO;
  MTKView* view = [[MTKView alloc] initWithFrame:self.window.bounds device:nil];
  view.preferredFramesPerSecond = 60;
  vc.view = view;

  std::string lib = moppe::platform::asset_path ("moppe.metallib");
  moppe::render::Renderer* renderer =
    moppe::render::create_metal_renderer ((__bridge void*)view, lib);
  vc.renderer = renderer;
  view.delegate = vc;

  self.viewController = vc;
  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];
  [vc connectGameController];

  g_game->setup (
    *renderer, (int)view.bounds.size.width, (int)view.bounds.size.height);
  return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
  [self.viewController disconnectGameController];
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
  [self.viewController connectGameController];
}
@end

// ------------------------------------------------------------------

namespace moppe::platform {
  std::unique_ptr<terrain::FieldEvaluator> create_field_evaluator () {
    if (@available (tvOS 26.0, *)) {
      try {
        auto evaluator = std::make_unique<terrain::metal::MetalEvaluator> (
          asset_path ("moppe.metallib"));
        std::cerr << "moppe: Metal 4 field evaluator enabled" << std::endl;
        return evaluator;
      } catch (const std::exception& error) {
        std::cerr << "moppe: Metal field evaluator unavailable: "
                  << error.what () << std::endl;
      }
    }
    return {};
  }

  std::unique_ptr<terrain::StreamPowerEvolutionBackend>
  create_stream_power_evolution_backend () {
    // The portable backend remains faster when world generation and loading
    // presentation contend for Apple TV's GPU.
    return {};
  }

  int run (Game& game, const Config& config) {
    g_game = &game;
    g_config = config;
    static char name[] = "moppe";
    static char* argv[] = { name, nullptr };
    @autoreleasepool {
      return UIApplicationMain (
        1, argv, nil, NSStringFromClass ([MoppeTVAppDelegate class]));
    }
  }

  void request_quit () {
    // The system owns the tvOS application lifecycle.
  }

  void set_window_title (const std::string&) {}

  Insets safe_insets () {
    Insets result;
    if (!g_window)
      return result;
    const UIEdgeInsets insets = g_window.safeAreaInsets;
    result.left = (float)insets.left;
    result.top = (float)insets.top;
    result.right = (float)insets.right;
    result.bottom = (float)insets.bottom;
    return result;
  }
}

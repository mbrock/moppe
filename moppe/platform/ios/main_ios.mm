// iOS platform layer: UIKit + MTKView, with touch zones synthesized
// onto the same autorepeat-free key-edge model the game already
// speaks.  Landscape only.
//
// Touch zones (fractions of the view, landscape):
//   bottom-left eighth/eighth        steer left / steer right
//   bottom-right quarter             throttle
//   mid-right band (below rocket)    brake / reverse
//   right edge, upper-middle         rocket jump (Space)
//   top-right corner                 camera cycle (Tab)
//   top-left corner                  mount/dismount (synthesizes the
//                                    secret 7-5-R combo; on the game
//                                    over screen the R revives)

#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>

#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>

#include <map>

using moppe::platform::Game;
using moppe::platform::Key;
using moppe::platform::Config;

static Game* g_game = 0;
static Config g_config;

// ------------------------------------------------------------------

@interface MoppeTouchView : MTKView
@end

@implementation MoppeTouchView {
  std::map<void*, int> m_touch_keys;   // UITouch* -> Key
  std::map<int, int> m_key_refs;       // Key -> live touch count
}

- (instancetype) initWithFrame: (CGRect) frame {
  self = [super initWithFrame: frame device: nil];
  self.multipleTouchEnabled = YES;
  return self;
}

- (Key) zoneForPoint: (CGPoint) p {
  // Zones live inside the safe area (notch / home indicator).
  const UIEdgeInsets si = self.safeAreaInsets;
  const CGFloat w = self.bounds.size.width - si.left - si.right;
  const CGFloat h = self.bounds.size.height - si.top - si.bottom;
  if (w <= 0 || h <= 0)
    return Key::Unknown;
  const CGFloat x = (p.x - si.left) / w, y = (p.y - si.top) / h;

  if (y < 0.18) {
    if (x > 0.85) return Key::Tab;      // camera cycle
    if (x < 0.15) return Key::Seven;    // mount/dismount combo
  }
  if (x < 0.25 && y > 0.4)
    return (x < 0.125) ? Key::A : Key::D;
  if (x > 0.75) {
    if (y > 0.5) return Key::W;         // throttle
    if (y > 0.2) return Key::Space;     // rocket
  }
  if (x > 0.55 && x <= 0.75 && y > 0.5)
    return Key::S;                      // brake / reverse
  return Key::Unknown;
}

- (void) pressKey: (Key) k down: (bool) down {
  if (!g_game || k == Key::Unknown)
    return;
  if (k == Key::Seven && down) {
    // The secret dismount combo, typed for you.  The final R stays
    // held until the finger lifts (and revives on the game-over
    // screen, where only R works).
    g_game->key (Key::Seven, true);
    g_game->key (Key::Seven, false);
    g_game->key (Key::Five, true);
    g_game->key (Key::Five, false);
    g_game->key (Key::R, true);
    return;
  }
  if (k == Key::Seven && !down) {
    g_game->key (Key::R, false);
    return;
  }
  g_game->key (k, down);
}

- (void) touchesBegan: (NSSet<UITouch*>*) touches
	    withEvent: (UIEvent*) event {
  for (UITouch* t in touches) {
    Key k = [self zoneForPoint: [t locationInView: self]];
    if (k == Key::Unknown)
      continue;
    m_touch_keys[(__bridge void*) t] = (int) k;
    // Reference-count per key: a second finger in the same zone
    // must not re-press, and lifting one of two must not release.
    if (++m_key_refs[(int) k] == 1)
      [self pressKey: k down: true];
  }
}

- (void) endTouches: (NSSet<UITouch*>*) touches {
  for (UITouch* t in touches) {
    std::map<void*, int>::iterator it =
      m_touch_keys.find ((__bridge void*) t);
    if (it == m_touch_keys.end ())
      continue;
    if (--m_key_refs[it->second] <= 0) {
      m_key_refs.erase (it->second);
      [self pressKey: (Key) it->second down: false];
    }
    m_touch_keys.erase (it);
  }
}

- (void) touchesEnded: (NSSet<UITouch*>*) touches
	    withEvent: (UIEvent*) event {
  [self endTouches: touches];
}

// Interrupted touches (calls, Control Center) must release their
// keys or the throttle sticks open.
- (void) touchesCancelled: (NSSet<UITouch*>*) touches
	        withEvent: (UIEvent*) event {
  [self endTouches: touches];
}
@end

// ------------------------------------------------------------------

@interface MoppeViewController : UIViewController <MTKViewDelegate>
@property (nonatomic, assign) moppe::render::Renderer* renderer;
@end

@implementation MoppeViewController {
  double m_last_time;
}

- (BOOL) prefersHomeIndicatorAutoHidden { return YES; }
- (BOOL) prefersStatusBarHidden { return YES; }

- (void) drawInMTKView: (MTKView*) view {
  if (!g_game || !self.renderer)
    return;
  const double t = moppe::platform::now ();
  float dt = m_last_time > 0 ? (float) (t - m_last_time) : 1.0f / 60;
  m_last_time = t;
  if (dt > 0.05f)
    dt = 0.05f;
  if (dt > 0)
    g_game->tick (dt);
  g_game->render (*self.renderer);
}

- (void) mtkView: (MTKView*) view
	 drawableSizeWillChange: (CGSize) size {
  if (!g_game)
    return;
  const float scale = (float) view.contentScaleFactor;
  g_game->resize ((int) (size.width / scale),
		  (int) (size.height / scale));
}
@end

// ------------------------------------------------------------------

@interface MoppeAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation MoppeAppDelegate

- (BOOL) application: (UIApplication*) application
	 didFinishLaunchingWithOptions: (NSDictionary*) options {
  self.window = [[UIWindow alloc]
		  initWithFrame: [UIScreen mainScreen].bounds];

  MoppeViewController* vc = [[MoppeViewController alloc] init];
  MoppeTouchView* view =
    [[MoppeTouchView alloc] initWithFrame: self.window.bounds];
  view.preferredFramesPerSecond = 60;
  vc.view = view;

  std::string lib = moppe::platform::asset_path ("moppe.metallib");
  moppe::render::Renderer* renderer =
    moppe::render::create_metal_renderer ((__bridge void*) view, lib);
  vc.renderer = renderer;
  view.delegate = vc;

  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];

  const float scale = (float) view.contentScaleFactor;
  g_game->setup (*renderer,
		 (int) (view.drawableSize.width / scale),
		 (int) (view.drawableSize.height / scale));
  return YES;
}
@end

// ------------------------------------------------------------------

namespace moppe {
namespace platform {
  int
  run (Game& game, const Config& config) {
    g_game = &game;
    g_config = config;
    static char name[] = "moppe";
    static char* argv[] = { name, 0 };
    @autoreleasepool {
      return UIApplicationMain (1, argv, nil,
				NSStringFromClass
				  ([MoppeAppDelegate class]));
    }
  }

  void
  request_quit () {
    // iOS apps don't exit programmatically.
  }

  Insets
  safe_insets () {
    Insets r;
    UIWindow* w = nil;
    for (UIWindow* win in [UIApplication sharedApplication].windows)
      if (win.isKeyWindow) { w = win; break; }
    if (!w)
      return r;
    const UIEdgeInsets si = w.safeAreaInsets;
    r.left = (float) si.left;
    r.top = (float) si.top;
    r.right = (float) si.right;
    r.bottom = (float) si.bottom;
    return r;
  }
}
}

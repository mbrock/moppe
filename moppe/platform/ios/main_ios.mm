// iOS platform layer: UIKit + MTKView with two floating analog controls.
// Landscape only.
//
//   lower-left half                  steer + throttle/brake/reverse
//   lower-right half                 continuous jump-jet boost
//   top-right corner                 camera cycle (Tab)
//   top-left corner                  mount/dismount (synthesizes the
//                                    secret 7-5-R combo; on the game
//                                    over screen the R revives)

#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/terrain/evaluator.hh>

#include <algorithm>
#include <cmath>
#include <map>

using moppe::platform::Config;
using moppe::platform::ControlState;
using moppe::platform::Game;
using moppe::platform::Key;

static Game* g_game = 0;
static Config g_config;

static float
control_axis (CGFloat displacement, CGFloat dead_zone, CGFloat travel) {
  const CGFloat magnitude = std::abs (displacement);
  if (magnitude <= dead_zone)
    return 0;
  const float value =
    (float)std::min (1.0, (magnitude - dead_zone) / (travel - dead_zone));
  return displacement < 0 ? -value : value;
}

// ------------------------------------------------------------------

@interface MoppeTouchView : MTKView
@end

@implementation MoppeTouchView {
  std::map<void*, int> m_touch_keys; // corner action touches
  std::map<int, int> m_key_refs;
  UITouch* m_drive_touch;
  UITouch* m_boost_touch;
  CGPoint m_drive_center;
  CGPoint m_boost_center;
  float m_steer;
  float m_drive;
  float m_boost;
  CAShapeLayer* m_drive_base;
  CAShapeLayer* m_drive_knob;
  CAShapeLayer* m_boost_base;
  CAShapeLayer* m_boost_knob;
}

- (CAShapeLayer*)controlLayerWithRadius:(CGFloat)radius
                                  color:(UIColor*)color
                                   fill:(CGFloat)fill {
  CAShapeLayer* layer = [CAShapeLayer layer];
  layer.bounds = CGRectMake (0, 0, radius * 2, radius * 2);
  layer.path = [UIBezierPath bezierPathWithOvalInRect:layer.bounds].CGPath;
  layer.fillColor = [color colorWithAlphaComponent:fill].CGColor;
  layer.strokeColor = [color colorWithAlphaComponent:0.5].CGColor;
  layer.lineWidth = 1.5;
  layer.hidden = YES;
  layer.zPosition = 100;
  layer.contentsScale = [UIScreen mainScreen].scale;
  [self.layer addSublayer:layer];
  return layer;
}

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame device:nil];
  if (self) {
    self.multipleTouchEnabled = YES;
    UIColor* drive = [UIColor colorWithRed:0.72 green:0.86 blue:1.0 alpha:1.0];
    UIColor* boost = [UIColor colorWithRed:0.25 green:0.72 blue:1.0 alpha:1.0];
    m_drive_base = [self controlLayerWithRadius:62 color:drive fill:0.08];
    m_drive_knob = [self controlLayerWithRadius:22 color:drive fill:0.28];
    m_boost_base = [self controlLayerWithRadius:56 color:boost fill:0.09];
    m_boost_knob = [self controlLayerWithRadius:23 color:boost fill:0.34];
  }
  return self;
}

- (BOOL)normalizedPoint:(CGPoint)p x:(CGFloat*)x y:(CGFloat*)y {
  const UIEdgeInsets si = self.safeAreaInsets;
  const CGFloat w = self.bounds.size.width - si.left - si.right;
  const CGFloat h = self.bounds.size.height - si.top - si.bottom;
  if (w <= 0 || h <= 0)
    return NO;
  *x = (p.x - si.left) / w;
  *y = (p.y - si.top) / h;
  return *x >= 0 && *x <= 1 && *y >= 0 && *y <= 1;
}

- (Key)actionForPoint:(CGPoint)p {
  CGFloat x, y;
  if (![self normalizedPoint:p x:&x y:&y])
    return Key::Unknown;

  if (y < 0.18) {
    if (x > 0.82)
      return Key::Tab;
    if (x < 0.18)
      return Key::Seven;
  }
  return Key::Unknown;
}

- (void)sendControls {
  if (!g_game)
    return;
  ControlState state;
  state.steer = m_steer;
  state.drive = m_drive;
  state.boost = m_boost;
  g_game->controls (state);
}

- (void)showBase:(CAShapeLayer*)base
            knob:(CAShapeLayer*)knob
          center:(CGPoint)center {
  base.position = center;
  knob.position = center;
  base.hidden = NO;
  knob.hidden = NO;
}

- (void)updateDrive:(CGPoint)p {
  const CGFloat travel = 62, dead_zone = 7;
  const CGFloat dx = p.x - m_drive_center.x;
  const CGFloat dy = m_drive_center.y - p.y;
  const float raw_steer = control_axis (dx, dead_zone, travel);
  const float magnitude = std::abs (raw_steer);
  m_steer =
    (raw_steer < 0 ? -1.0f : 1.0f) * magnitude * (0.35f + 0.65f * magnitude);
  m_drive = control_axis (dy, dead_zone, travel);

  const CGFloat length = std::sqrt (dx * dx + dy * dy);
  const CGFloat scale = length > travel ? travel / length : 1;
  m_drive_knob.position =
    CGPointMake (m_drive_center.x + dx * scale, m_drive_center.y - dy * scale);
}

- (void)updateBoost:(CGPoint)p {
  const CGFloat travel = 56, dead_zone = 5;
  const CGFloat dy = m_boost_center.y - p.y;
  m_boost = std::max (0.0f, control_axis (dy, dead_zone, travel));
  const CGFloat shown = std::max ((CGFloat)0, std::min (travel, dy));
  m_boost_knob.position =
    CGPointMake (m_boost_center.x, m_boost_center.y - shown);
}

- (void)pressKey:(Key)k down:(bool)down {
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

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  for (UITouch* t in touches) {
    const CGPoint p = [t locationInView:self];
    Key k = [self actionForPoint:p];
    if (k != Key::Unknown) {
      m_touch_keys[(__bridge void*)t] = (int)k;
      if (++m_key_refs[(int)k] == 1)
        [self pressKey:k down:true];
      continue;
    }

    CGFloat x, y;
    if (![self normalizedPoint:p x:&x y:&y] || y < 0.22)
      continue;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (x < 0.5 && !m_drive_touch) {
      m_drive_touch = t;
      m_drive_center = p;
      [self showBase:m_drive_base knob:m_drive_knob center:p];
      [self updateDrive:p];
    } else if (x >= 0.5 && !m_boost_touch) {
      m_boost_touch = t;
      m_boost_center = p;
      [self showBase:m_boost_base knob:m_boost_knob center:p];
      [self updateBoost:p];
    }
    [CATransaction commit];
    [self sendControls];
  }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  bool changed = false;
  for (UITouch* t in touches) {
    if (t == m_drive_touch) {
      [self updateDrive:[t locationInView:self]];
      changed = true;
    } else if (t == m_boost_touch) {
      [self updateBoost:[t locationInView:self]];
      changed = true;
    }
  }
  [CATransaction commit];
  if (changed)
    [self sendControls];
}

- (void)endTouches:(NSSet<UITouch*>*)touches {
  bool changed = false;
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  for (UITouch* t in touches) {
    std::map<void*, int>::iterator it = m_touch_keys.find ((__bridge void*)t);
    if (it != m_touch_keys.end ()) {
      if (--m_key_refs[it->second] <= 0) {
        m_key_refs.erase (it->second);
        [self pressKey:(Key)it->second down:false];
      }
      m_touch_keys.erase (it);
    } else if (t == m_drive_touch) {
      m_drive_touch = nil;
      m_steer = m_drive = 0;
      m_drive_base.hidden = YES;
      m_drive_knob.hidden = YES;
      changed = true;
    } else if (t == m_boost_touch) {
      m_boost_touch = nil;
      m_boost = 0;
      m_boost_base.hidden = YES;
      m_boost_knob.hidden = YES;
      changed = true;
    }
  }
  [CATransaction commit];
  if (changed)
    [self sendControls];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self endTouches:touches];
}

// Interrupted touches must release every axis so driving cannot stick.
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self endTouches:touches];
}
@end

// ------------------------------------------------------------------

@interface MoppeViewController : UIViewController <MTKViewDelegate>
@property (nonatomic, assign) moppe::render::Renderer* renderer;
@end

@implementation MoppeViewController {
  double m_last_time;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
  return YES;
}
- (BOOL)prefersStatusBarHidden {
  return YES;
}

- (void)drawInMTKView:(MTKView*)view {
  if (!g_game || !self.renderer)
    return;
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
  if (!g_game)
    return;
  const float scale = (float)view.contentScaleFactor;
  g_game->resize ((int)(size.width / scale), (int)(size.height / scale));
}
@end

// ------------------------------------------------------------------

@interface MoppeAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation MoppeAppDelegate

- (BOOL)application:(UIApplication*)application
  didFinishLaunchingWithOptions:(NSDictionary*)options {
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];

  MoppeViewController* vc = [[MoppeViewController alloc] init];
  MoppeTouchView* view =
    [[MoppeTouchView alloc] initWithFrame:self.window.bounds];
  view.preferredFramesPerSecond = 60;
  vc.view = view;

  std::string lib = moppe::platform::asset_path ("moppe.metallib");
  moppe::render::Renderer* renderer =
    moppe::render::create_metal_renderer ((__bridge void*)view, lib);
  vc.renderer = renderer;
  view.delegate = vc;

  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];

  const float scale = (float)view.contentScaleFactor;
  g_game->setup (*renderer,
                 (int)(view.drawableSize.width / scale),
                 (int)(view.drawableSize.height / scale));
  return YES;
}
@end

// ------------------------------------------------------------------

namespace moppe {
  namespace platform {
    std::unique_ptr<terrain::FieldEvaluator> create_field_evaluator () {
      // The game still targets iOS 15; Metal 4 becomes available at iOS 26.
      // Keep the portable backend until the deployment policy changes.
      return {};
    }

    int run (Game& game, const Config& config) {
      g_game = &game;
      g_config = config;
      static char name[] = "moppe";
      static char* argv[] = { name, 0 };
      @autoreleasepool {
        return UIApplicationMain (
          1, argv, nil, NSStringFromClass ([MoppeAppDelegate class]));
      }
    }

    void request_quit () {
      // iOS apps don't exit programmatically.
    }

    void set_window_title (const std::string&) {}

    Insets safe_insets () {
      Insets r;
      UIWindow* w = nil;
      for (UIWindow* win in [UIApplication sharedApplication].windows)
        if (win.isKeyWindow) {
          w = win;
          break;
        }
      if (!w)
        return r;
      const UIEdgeInsets si = w.safeAreaInsets;
      r.left = (float)si.left;
      r.top = (float)si.top;
      r.right = (float)si.right;
      r.bottom = (float)si.bottom;
      return r;
    }
  }
}

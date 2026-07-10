// macOS platform layer: NSApplication + MTKView, autorepeat-filtered
// keyboard edges, monotonic-clock ticks.  Replaces GLUT.

#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>

#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

using moppe::platform::Game;
using moppe::platform::Key;

// ------------------------------------------------------------------

static Key
map_key (NSEvent* event) {
  switch (event.keyCode) {
  case 123: return Key::Left;
  case 124: return Key::Right;
  case 125: return Key::Down;
  case 126: return Key::Up;
  case 48:  return Key::Tab;
  case 49:  return Key::Space;
  case 53:  return Key::Escape;
  default: break;
  }
  NSString* chars = event.charactersIgnoringModifiers.lowercaseString;
  if (chars.length == 0)
    return Key::Unknown;
  switch ([chars characterAtIndex: 0]) {
  case 'w': return Key::W;
  case 'a': return Key::A;
  case 's': return Key::S;
  case 'd': return Key::D;
  case 'e': return Key::E;
  case 'n': return Key::N;
  case 'r': return Key::R;
  case 't': return Key::T;
  case 'y': return Key::Y;
  case '1': return Key::One;
  case '2': return Key::Two;
  case '3': return Key::Three;
  case '4': return Key::Four;
  case '5': return Key::Five;
  case '6': return Key::Six;
  case '7': return Key::Seven;
  default:  return Key::Unknown;
  }
}

// ------------------------------------------------------------------

@interface MoppeView : MTKView
@property (nonatomic, assign) Game* game;
@end

@implementation MoppeView {
  std::set<int> m_held;
}

- (BOOL) acceptsFirstResponder { return YES; }

- (void) keyDown: (NSEvent*) event {
  if (event.isARepeat)
    return;
  Key k = map_key (event);
  if (k == Key::Unknown)
    return;
  m_held.insert ((int) k);
  self.game->key (k, true);
}

- (void) keyUp: (NSEvent*) event {
  Key k = map_key (event);
  if (k == Key::Unknown)
    return;
  m_held.erase ((int) k);
  self.game->key (k, false);
}

- (void) releaseAllKeys {
  // Focus loss must not leave the throttle stuck open.
  for (std::set<int>::iterator it = m_held.begin ();
       it != m_held.end (); ++it)
    self.game->key ((Key) *it, false);
  m_held.clear ();
}
@end

// ------------------------------------------------------------------

static void
match_screen_refresh_rate (MoppeView* view) {
  NSScreen* screen = view.window.screen;
  const NSInteger hz = screen ? screen.maximumFramesPerSecond : 60;
  view.preferredFramesPerSecond = hz > 0 ? hz : 60;
}

static void
match_screen_render_size (MoppeView* view) {
  const NSSize points = view.bounds.size;
  const NSSize native = [view convertSizeToBacking: points];
  float scale = native.width * native.height > 12000000.0
    ? 0.5f : 1.0f;

  if (const char* requested = ::getenv ("MOPPE_RENDERSCALE")) {
    scale = (float) ::atof (requested);
    scale = std::max (0.25f, std::min (1.0f, scale));
  }

  const CGSize wanted = CGSizeMake
    (std::round (native.width * scale),
     std::round (native.height * scale));
  view.autoResizeDrawable = NO;
  if (view.drawableSize.width != wanted.width ||
      view.drawableSize.height != wanted.height)
    view.drawableSize = wanted;
}

// ------------------------------------------------------------------

@interface MoppeDelegate
  : NSObject <MTKViewDelegate, NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic, assign) Game* game;
@property (nonatomic, assign) moppe::render::Renderer* renderer;
@end

@implementation MoppeDelegate {
  double m_last_time;
}

- (instancetype) init {
  self = [super init];
  m_last_time = 0;
  return self;
}

- (void) drawInMTKView: (MTKView*) view {
  const double t = moppe::platform::now ();
  float dt = m_last_time > 0 ? (float) (t - m_last_time) : 1.0f / 60;
  m_last_time = t;
  if (dt > 0.05f)
    dt = 0.05f;   // anti-tunneling clamp, as in the GL build
  if (dt > 0)
    self.game->tick (dt);
  self.game->render (*self.renderer);
}

- (void) mtkView: (MTKView*) view
	 drawableSizeWillChange: (CGSize) size {
  self.game->resize ((int) view.bounds.size.width,
		     (int) view.bounds.size.height);
}

- (void) windowWillClose: (NSNotification*) note {
  [NSApp terminate: nil];
}

- (void) windowDidResignKey: (NSNotification*) note {
  NSWindow* w = note.object;
  if ([w.contentView isKindOfClass: [MoppeView class]])
    [(MoppeView*) w.contentView releaseAllKeys];
}

- (void) windowDidChangeScreen: (NSNotification*) note {
  NSWindow* window = note.object;
  if ([window.contentView isKindOfClass: [MoppeView class]]) {
    MoppeView* view = (MoppeView*) window.contentView;
    match_screen_refresh_rate (view);
    match_screen_render_size (view);
  }
}

- (void) windowDidResize: (NSNotification*) note {
  NSWindow* window = note.object;
  if ([window.contentView isKindOfClass: [MoppeView class]])
    match_screen_render_size ((MoppeView*) window.contentView);
}

- (NSApplicationTerminateReply) applicationShouldTerminate:
    (NSApplication*) app {
  return NSTerminateNow;
}
@end

// ------------------------------------------------------------------

namespace moppe {
namespace platform {
  int
  run (Game& game, const Config& config) {
    @autoreleasepool {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];

      NSRect frame = NSMakeRect (0, 0, config.width, config.height);
      NSUInteger style = NSWindowStyleMaskTitled
	| NSWindowStyleMaskClosable
	| NSWindowStyleMaskMiniaturizable
	| NSWindowStyleMaskResizable;
      NSWindow* window =
	[[NSWindow alloc] initWithContentRect: frame
				    styleMask: style
				      backing: NSBackingStoreBuffered
					defer: NO];
      window.title = [NSString stringWithUTF8String:
					config.title.c_str ()];
      [window center];

      MoppeView* view = [[MoppeView alloc] initWithFrame: frame
						  device: nil];
      view.game = &game;
      window.contentView = view;
      match_screen_refresh_rate (view);
      match_screen_render_size (view);

      // The renderer configures the view (formats, colorspace).
      std::string lib = asset_path (MOPPE_SHADER_NAME);
      render::Renderer* renderer =
	render::create_metal_renderer ((__bridge void*) view, lib);

      MoppeDelegate* delegate = [[MoppeDelegate alloc] init];
      delegate.game = &game;
      delegate.renderer = renderer;
      view.delegate = delegate;
      window.delegate = delegate;
      NSApp.delegate = delegate;

      [window makeKeyAndOrderFront: nil];
      [window makeFirstResponder: view];
      [NSApp activateIgnoringOtherApps: YES];

      if (config.fullscreen)
	[window toggleFullScreen: nil];

      game.setup (*renderer,
		  (int) view.bounds.size.width,
		  (int) view.bounds.size.height);

      [NSApp run];
      return 0;
    }
  }

  void
  request_quit () {
    dispatch_async (dispatch_get_main_queue (), ^{
      [NSApp terminate: nil];
    });
  }

  Insets
  safe_insets () {
    return Insets ();
  }
}
}

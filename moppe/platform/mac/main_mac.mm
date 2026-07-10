// macOS platform layer: NSApplication + MTKView, autorepeat-filtered
// keyboard edges, monotonic-clock ticks.  Replaces GLUT.

#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>

#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>

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
  case 'r': return Key::R;
  case '5': return Key::Five;
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
  const float scale = view.window
    ? (float) view.window.backingScaleFactor : 1.0f;
  self.game->resize ((int) (size.width / scale),
		     (int) (size.height / scale));
}

- (void) windowWillClose: (NSNotification*) note {
  [NSApp terminate: nil];
}

- (void) windowDidResignKey: (NSNotification*) note {
  NSWindow* w = note.object;
  if ([w.contentView isKindOfClass: [MoppeView class]])
    [(MoppeView*) w.contentView releaseAllKeys];
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
      view.preferredFramesPerSecond = 60;
      window.contentView = view;

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

      const float scale = (float) window.backingScaleFactor;
      game.setup (*renderer,
		  (int) (view.drawableSize.width / scale),
		  (int) (view.drawableSize.height / scale));

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

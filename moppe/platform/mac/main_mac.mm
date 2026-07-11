// macOS platform layer: NSApplication + MTKView, autorepeat-filtered
// keyboard edges, monotonic-clock ticks.  Replaces GLUT.

#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>

#include <moppe/platform/platform.hh>
#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/terrain/metal/metal_evaluator.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

using moppe::platform::Game;
using moppe::platform::Key;
using moppe::platform::PointerButton;

// ------------------------------------------------------------------

static Key map_key (NSEvent* event) {
  switch (event.keyCode) {
  case 123:
    return Key::Left;
  case 124:
    return Key::Right;
  case 125:
    return Key::Down;
  case 126:
    return Key::Up;
  case 48:
    return Key::Tab;
  case 49:
    return Key::Space;
  case 53:
    return Key::Escape;
  default:
    break;
  }
  NSString* chars = event.charactersIgnoringModifiers.lowercaseString;
  if (chars.length == 0)
    return Key::Unknown;
  switch ([chars characterAtIndex:0]) {
  case 'w':
    return Key::W;
  case 'a':
    return Key::A;
  case 's':
    return Key::S;
  case 'd':
    return Key::D;
  case 'e':
    return Key::E;
  case 'n':
    return Key::N;
  case 'r':
    return Key::R;
  case 't':
    return Key::T;
  case 'y':
    return Key::Y;
  case '1':
    return Key::One;
  case '2':
    return Key::Two;
  case '3':
    return Key::Three;
  case '4':
    return Key::Four;
  case '5':
    return Key::Five;
  case '6':
    return Key::Six;
  case '7':
    return Key::Seven;
  default:
    return Key::Unknown;
  }
}

// ------------------------------------------------------------------

@interface MoppeView : MTKView
@property (nonatomic, assign) Game* game;
@end

@implementation MoppeView {
  std::set<int> m_held;
  std::set<int> m_pointer_buttons;
  float m_pointer_x;
  float m_pointer_y;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (NSPoint)pointerPoint:(NSEvent*)event {
  const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
  return NSMakePoint (p.x, self.bounds.size.height - p.y);
}

- (void)updatePointer:(NSEvent*)event {
  const NSPoint p = [self pointerPoint:event];
  const float dx = p.x - m_pointer_x;
  const float dy = p.y - m_pointer_y;
  m_pointer_x = p.x;
  m_pointer_y = p.y;
  self.game->pointer_move (m_pointer_x, m_pointer_y, dx, dy);
}

- (void)pointerButton:(PointerButton)button
                 down:(bool)down
                event:(NSEvent*)event {
  const NSPoint p = [self pointerPoint:event];
  m_pointer_x = p.x;
  m_pointer_y = p.y;
  if (down)
    m_pointer_buttons.insert ((int)button);
  else
    m_pointer_buttons.erase ((int)button);
  self.game->pointer_button (button, down, m_pointer_x, m_pointer_y);
}

- (void)keyDown:(NSEvent*)event {
  if (event.isARepeat)
    return;
  Key k = map_key (event);
  if (k == Key::Unknown)
    return;
  m_held.insert ((int)k);
  self.game->key (k, true);
}

- (void)keyUp:(NSEvent*)event {
  Key k = map_key (event);
  if (k == Key::Unknown)
    return;
  m_held.erase ((int)k);
  self.game->key (k, false);
}

- (void)mouseMoved:(NSEvent*)event {
  [self updatePointer:event];
}

- (void)mouseDragged:(NSEvent*)event {
  [self updatePointer:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
  [self updatePointer:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
  [self updatePointer:event];
}

- (void)mouseDown:(NSEvent*)event {
  [self pointerButton:PointerButton::Primary down:true event:event];
}

- (void)mouseUp:(NSEvent*)event {
  [self pointerButton:PointerButton::Primary down:false event:event];
}

- (void)rightMouseDown:(NSEvent*)event {
  [self pointerButton:PointerButton::Secondary down:true event:event];
}

- (void)rightMouseUp:(NSEvent*)event {
  [self pointerButton:PointerButton::Secondary down:false event:event];
}

- (void)otherMouseDown:(NSEvent*)event {
  [self pointerButton:PointerButton::Middle down:true event:event];
}

- (void)otherMouseUp:(NSEvent*)event {
  [self pointerButton:PointerButton::Middle down:false event:event];
}

- (void)scrollWheel:(NSEvent*)event {
  // Trackpads and the Magic Mouse keep emitting inertial events after the
  // fingers have stopped.  That feels natural in a document but makes an
  // orbital camera run away from an otherwise modest zoom gesture.
  if (event.momentumPhase != NSEventPhaseNone)
    return;
  const NSPoint p = [self pointerPoint:event];
  m_pointer_x = p.x;
  m_pointer_y = p.y;
  self.game->pointer_scroll (
    m_pointer_x, m_pointer_y, (float)event.scrollingDeltaY);
}

- (void)releaseAllKeys {
  // Focus loss must not leave the throttle stuck open.
  for (std::set<int>::iterator it = m_held.begin (); it != m_held.end (); ++it)
    self.game->key ((Key)*it, false);
  m_held.clear ();
  for (int button : m_pointer_buttons)
    self.game->pointer_button (
      (PointerButton)button, false, m_pointer_x, m_pointer_y);
  m_pointer_buttons.clear ();
}
@end

// ------------------------------------------------------------------

static void match_screen_refresh_rate (MoppeView* view) {
  NSScreen* screen = view.window.screen;
  const NSInteger hz = screen ? screen.maximumFramesPerSecond : 60;
  view.preferredFramesPerSecond = hz > 0 ? hz : 60;
}

static void match_screen_render_size (MoppeView* view) {
  const NSSize points = view.bounds.size;
  // The drawable is the final presentation surface, so it must stay at the
  // screen's native backing resolution.  The Metal renderer applies the
  // optional scene render scale only to its expensive offscreen 3D targets;
  // keeping the drawable native lets the HUD remain Retina-sharp.
  //
  // Do not derive this from convertSizeToBacking:.  MTKView can fold a
  // manually overridden drawableSize back into that conversion after a
  // Space/focus transition.  The window's backing scale is stable.
  const CGFloat backing = view.window ? view.window.backingScaleFactor
                                      : NSScreen.mainScreen.backingScaleFactor;
  const CGSize wanted = CGSizeMake (std::round (points.width * backing),
                                    std::round (points.height * backing));
  view.autoResizeDrawable = NO;
  if (view.drawableSize.width != wanted.width ||
      view.drawableSize.height != wanted.height)
    view.drawableSize = wanted;
}

static const char* pixel_format_name (MTLPixelFormat format) {
  switch (format) {
  case MTLPixelFormatBGRA8Unorm:
    return "BGRA8Unorm";
  case MTLPixelFormatRGBA16Float:
    return "RGBA16Float";
  case MTLPixelFormatDepth32Float_Stencil8:
    return "Depth32Float_Stencil8";
  case MTLPixelFormatInvalid:
    return "none";
  default:
    return "other";
  }
}

static void log_runtime_parameters (MoppeView* view) {
  NSScreen* screen = view.window.screen;
  CAMetalLayer* layer = (CAMetalLayer*)view.layer;
  const NSSize points = view.bounds.size;
  const CGSize pixels = view.drawableSize;
  const NSInteger maximum_fps = screen ? screen.maximumFramesPerSecond : 0;
  const CGFloat backing = view.window ? view.window.backingScaleFactor : 1.0;

  std::cerr << "moppe: runtime parameters\n"
            << "  display: maximum-fps=" << maximum_fps
            << ", preferred-fps=" << view.preferredFramesPerSecond
            << ", display-sync=" << (layer.displaySyncEnabled ? "on" : "off")
            << '\n'
            << "  view: " << (int)points.width << 'x' << (int)points.height
            << " points, " << (int)pixels.width << 'x' << (int)pixels.height
            << " pixels, backing-scale=" << backing << '\n'
            << "  drawable: color=" << pixel_format_name (view.colorPixelFormat)
            << ", depth=" << pixel_format_name (view.depthStencilPixelFormat)
            << ", samples=" << view.sampleCount
            << ", framebuffer-only=" << (view.framebufferOnly ? "yes" : "no")
            << ", edr="
            << (layer.wantsExtendedDynamicRangeContent ? "on" : "off")
            << std::endl;
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

- (instancetype)init {
  self = [super init];
  m_last_time = 0;
  return self;
}

- (void)drawInMTKView:(MTKView*)view {
  const double t = moppe::platform::now ();
  float dt = m_last_time > 0 ? (float)(t - m_last_time) : 1.0f / 60;
  m_last_time = t;
  if (dt > 0.05f)
    dt = 0.05f; // anti-tunneling clamp, as in the GL build
  if (dt > 0)
    self.game->tick (dt);
  self.game->render (*self.renderer);
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  self.game->resize ((int)view.bounds.size.width, (int)view.bounds.size.height);
}

- (void)windowWillClose:(NSNotification*)note {
  [NSApp terminate:nil];
}

- (void)windowDidResignKey:(NSNotification*)note {
  NSWindow* w = note.object;
  if ([w.contentView isKindOfClass:[MoppeView class]])
    [(MoppeView*)w.contentView releaseAllKeys];
}

- (void)windowDidChangeScreen:(NSNotification*)note {
  NSWindow* window = note.object;
  if ([window.contentView isKindOfClass:[MoppeView class]]) {
    MoppeView* view = (MoppeView*)window.contentView;
    match_screen_refresh_rate (view);
    match_screen_render_size (view);
  }
}

- (void)windowDidResize:(NSNotification*)note {
  NSWindow* window = note.object;
  if ([window.contentView isKindOfClass:[MoppeView class]])
    match_screen_render_size ((MoppeView*)window.contentView);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
  return NSTerminateNow;
}
@end

// ------------------------------------------------------------------

namespace moppe {
  namespace platform {
    std::unique_ptr<terrain::FieldEvaluator> create_field_evaluator () {
      try {
        return std::make_unique<terrain::metal::MetalEvaluator> (
          asset_path (MOPPE_SHADER_NAME));
      } catch (const std::exception& error) {
        std::cerr << "moppe: Metal field evaluator unavailable: "
                  << error.what () << std::endl;
        return {};
      }
    }

    int run (Game& game, const Config& config) {
      @autoreleasepool {
        // Metal reads its HUD configuration before the device is created.
        const char* hud = ::getenv ("MOPPE_METAL_HUD");
        if (hud && std::string (hud) != "0")
          ::setenv ("MTL_HUD_ENABLED", "1", 0);
        if (::getenv ("MOPPE_METAL_CAPTURE"))
          ::setenv ("MTL_CAPTURE_ENABLED", "1", 0);

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect (0, 0, config.width, config.height);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable;
        NSWindow* window =
          [[NSWindow alloc] initWithContentRect:frame
                                      styleMask:style
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
        window.title = [NSString stringWithUTF8String:config.title.c_str ()];
        [window center];

        MoppeView* view = [[MoppeView alloc] initWithFrame:frame device:nil];
        view.game = &game;
        if (config.capture_frames)
          view.framebufferOnly = NO;
        window.contentView = view;
        match_screen_refresh_rate (view);
        match_screen_render_size (view);

        // The renderer configures the view (formats, colorspace).
        std::string lib = asset_path (MOPPE_SHADER_NAME);
        render::Renderer* renderer =
          render::create_metal_renderer ((__bridge void*)view, lib);
        log_runtime_parameters (view);

        MoppeDelegate* delegate = [[MoppeDelegate alloc] init];
        delegate.game = &game;
        delegate.renderer = renderer;
        view.delegate = delegate;
        window.delegate = delegate;
        NSApp.delegate = delegate;

        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        window.acceptsMouseMovedEvents = YES;
        [NSApp activateIgnoringOtherApps:YES];

        if (config.fullscreen)
          [window toggleFullScreen:nil];

        game.setup (
          *renderer, (int)view.bounds.size.width, (int)view.bounds.size.height);

        [NSApp run];
        return 0;
      }
    }

    void request_quit () {
      dispatch_async (dispatch_get_main_queue (), ^{ [NSApp terminate:nil]; });
    }

    Insets safe_insets () {
      return Insets ();
    }
  }
}

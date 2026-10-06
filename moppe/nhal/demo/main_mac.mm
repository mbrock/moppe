// The NHAL demo on macOS: a window whose layer is a CAMetalLayer, driven by
// a timer on the main thread. `--capture PATH --frames N` renders N frames,
// writes the last one as a PNG, and quits, without taking focus.
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <moppe/nhal/demo/scene.hh>
#include <moppe/nhal/metal/metal_device.hh>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

using namespace moppe::nhal;

namespace {
  std::string read_file (const std::string& path) {
    std::ifstream file (path);
    if (!file)
      throw std::runtime_error ("cannot read " + path);
    std::ostringstream text;
    text << file.rdbuf ();
    return text.str ();
  }

  void write_png (const Capture& capture, const std::string& path) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
    CGDataProviderRef data = CGDataProviderCreateWithData (
      nullptr, capture.pixels.data (), capture.pixels.size (), nullptr);
    CGImageRef image = CGImageCreate (
      capture.width, capture.height, 8, 32, capture.row_bytes, space,
      CGBitmapInfo (kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little,
      data, nullptr,
      false, kCGRenderingIntentDefault);
    NSURL* url = [NSURL fileURLWithPath:@(path.c_str ())];
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL (
      (__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1,
      nullptr);
    CGImageDestinationAddImage (destination, image, nullptr);
    CGImageDestinationFinalize (destination);
    CFRelease (destination);
    CGImageRelease (image);
    CGDataProviderRelease (data);
    CGColorSpaceRelease (space);
  }
}

@interface DemoView : NSView
@end

@implementation DemoView
- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer {
  return YES;
}
@end

@interface DemoDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (nonatomic) std::string shaderPath;
@property (nonatomic) std::string capturePath;
@property (nonatomic) int frames;
@property (nonatomic) bool native;
@end

@implementation DemoDelegate {
  NSWindow* _window;
  DemoView* _view;
  std::unique_ptr<Device> _device;
  std::unique_ptr<demo::Scene> _scene;
  std::array<std::string, 13> _msl;
  std::chrono::steady_clock::time_point _start;
  std::chrono::steady_clock::time_point _last;
  int _rendered;
  std::set<unsigned short> _keys;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  const NSRect frame = NSMakeRect (0, 0, 1280, 720);
  _window = [[NSWindow alloc]
    initWithContentRect:frame
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                        | NSWindowStyleMaskResizable
                        | NSWindowStyleMaskMiniaturizable
                backing:NSBackingStoreBuffered
                  defer:NO];
  _window.title = @"NHAL demo (Metal 4)";
  _window.delegate = self;
  _view = [[DemoView alloc] initWithFrame:frame];
  _view.wantsLayer = YES;
  _window.contentView = _view;
  [_window center];
  if (self.capturePath.empty ()) {
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  } else {
    [_window orderBack:nil];
  }

  CAMetalLayer* layer = (CAMetalLayer*)_view.layer;
  const CGFloat scale = _window.backingScaleFactor;
  layer.contentsScale = scale;
  layer.drawableSize = CGSizeMake (frame.size.width * scale,
                                   frame.size.height * scale);
  try {
    _device = create_metal_device (layer, Format::bgra8_unorm);
    // One MSL document per program stage, as luv-shaderc writes them.
    const char* files[] = { "terrain.vertex", "terrain.fragment",
                            "trees.vertex", "trees.fragment",
                            "sky.vertex", "sky.fragment",
                            "tonemap.vertex", "tonemap.fragment",
                            "forest_wind.compute", "terrain_shadow.vertex",
                            "trees_shadow.vertex", "resolve.vertex",
                            "resolve.fragment" };
    for (int i = 0; i < 13; ++i)
      _msl[i] = read_file (self.shaderPath + "/" + files[i] + ".metal");
    auto code = [&] (int i) { return StageCode { _msl[i], {} }; };
    _scene = std::make_unique<demo::Scene> (
      *_device, demo::Shaders { code (0), code (1), code (2), code (3),
                                code (4), code (5), code (6), code (7),
                                code (8), code (9), code (10), code (11),
                                code (12) });
    _scene->set_temporal (!self.native);
    std::cerr << "NHAL demo: " << _device->info ().backend << " on "
              << _device->info ().adapter << ", " << _scene->tree_count ()
              << " trees" << std::endl;
  } catch (const std::exception& error) {
    std::cerr << "NHAL demo: " << error.what () << std::endl;
    exit (-1);
  }
  _start = _last = std::chrono::steady_clock::now ();
  // WASD moves, the arrows look, Q and E sink and rise, Shift hurries.
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                | NSEventMaskKeyUp
                                        handler:^NSEvent* (NSEvent* event) {
    // U switches between temporal upscaling and native 4x MSAA.
    if (event.type == NSEventTypeKeyDown && event.keyCode == 32
        && !event.isARepeat && self->_scene) {
      self->_scene->set_temporal (!self->_scene->temporal ());
      std::cerr << "NHAL demo: "
                << (self->_scene->temporal () ? "temporal upscaling"
                                              : "native 4x MSAA")
                << std::endl;
    }
    if (event.type == NSEventTypeKeyDown)
      self->_keys.insert (event.keyCode);
    else
      self->_keys.erase (event.keyCode);
    return nil;
  }];
  [NSTimer scheduledTimerWithTimeInterval:1.0 / 120
                                   target:self
                                 selector:@selector (tick:)
                                 userInfo:nil
                                  repeats:YES];
}

- (void)tick:(NSTimer*)timer {
  const double seconds =
    self.capturePath.empty ()
      ? std::chrono::duration<double> (std::chrono::steady_clock::now ()
                                       - _start)
          .count ()
      : 12.0 + _rendered / 60.0;
  const auto now = std::chrono::steady_clock::now ();
  const double step =
    std::min (0.1, std::chrono::duration<double> (now - _last).count ());
  _last = now;
  auto held = [&] (unsigned short key) { return _keys.count (key) ? 1.0f : 0.0f; };
  demo::Flight flight;
  flight.forward = held (13) - held (1);  // W, S
  flight.strafe = held (2) - held (0);    // D, A
  flight.rise = held (14) - held (12);    // E, Q
  flight.turn = held (124) - held (123);  // right, left
  flight.pitch = held (126) - held (125); // up, down
  flight.boost = (NSEvent.modifierFlags & NSEventModifierFlagShift) != 0;
  try {
    _scene->fly (flight, step);
    if (!_scene->render (seconds))
      return;
    ++_rendered;
    const bool last = !self.capturePath.empty () && _rendered == self.frames;
    if (last) {
      const std::string path = self.capturePath;
      _device->capture_frame ([path] (const Capture& capture) {
        write_png (capture, path);
        std::cerr << "NHAL demo: wrote " << path << " (" << capture.width
                  << "x" << capture.height << ")" << std::endl;
      });
    }
    _device->end_frame ();
    if (_rendered % 120 == 0 || last) {
      std::cerr << "NHAL demo: GPU";
      for (const PassTiming& pass : _device->pass_timings ())
        std::fprintf (stderr, " %s %.2f ms", pass.label.c_str (),
                      pass.milliseconds);
      std::cerr << std::endl;
    }
    if (last) {
      _device->wait_idle ();
      [NSApp terminate:nil];
    }
  } catch (const std::exception& error) {
    std::cerr << "NHAL demo: " << error.what () << std::endl;
    exit (-1);
  }
}

- (void)windowDidResize:(NSNotification*)notification {
  if (!_device)
    return;
  const CGFloat scale = _window.backingScaleFactor;
  const NSSize size = _view.bounds.size;
  _device->resize_surface (std::uint32_t (size.width * scale),
                           std::uint32_t (size.height * scale));
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  _scene.reset ();
  _device.reset ();
}
@end

int main (int argc, const char** argv) {
  @autoreleasepool {
    DemoDelegate* delegate = [[DemoDelegate alloc] init];
    delegate.shaderPath = MOPPE_NHAL_DEMO_SHADERS;
    delegate.frames = 30;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--capture" && i + 1 < argc)
        delegate.capturePath = argv[++i];
      else if (arg == "--frames" && i + 1 < argc)
        delegate.frames = std::max (1, std::atoi (argv[++i]));
      else if (arg == "--shaders" && i + 1 < argc)
        delegate.shaderPath = argv[++i];
      else if (arg == "--native")
        delegate.native = true;
    }
    NSApplication* app = [NSApplication sharedApplication];
    app.activationPolicy = delegate.capturePath.empty ()
                             ? NSApplicationActivationPolicyRegular
                             : NSApplicationActivationPolicyAccessory;
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

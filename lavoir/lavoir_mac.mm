#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "lavoir/renderer.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

/// The Cocoa shim: a window, a view that adopts the renderer's layer,
/// a sixty-hertz timer, and a headless capture mode that writes one
/// frame as a PNG. Everything else is C++.

@interface LavoirView : NSView
@end

@implementation LavoirView {
  std::unique_ptr<lavoir::renderer> _renderer;
  NSTimer* _timer;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _renderer = std::make_unique<lavoir::renderer> ();
    self.wantsLayer = YES;
    self.layer = (__bridge CALayer*)_renderer->native_layer ();
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                              target:self
                                            selector:@selector (drawFrame:)
                                            userInfo:nil
                                             repeats:YES];
  }
  return self;
}

- (void)drawFrame:(NSTimer*)timer {
  (void)timer;
  const NSRect pixels = [self convertRectToBacking:self.bounds];
  _renderer->resize (
    static_cast<std::size_t> (pixels.size.width) * lavoir::pixel,
    static_cast<std::size_t> (pixels.size.height) * lavoir::pixel);
  _renderer->draw ();
}

@end

@interface LavoirApplication : NSObject <NSApplicationDelegate>
@end

@implementation LavoirApplication {
  NSWindow* _window;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  const NSRect frame = NSMakeRect (0, 0, 900, 650);
  _window = [[NSWindow alloc]
    initWithContentRect:frame
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable |
                        NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered
                  defer:NO];
  _window.title = @"Lavoir";
  _window.contentView = [[LavoirView alloc] initWithFrame:frame];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  (void)app;
  return YES;
}

@end

/// Photograph the workshop without opening a window: render one frame
/// and write it as a PNG.
static int capture_frame (const char* path, double seconds) {
  lavoir::renderer renderer;
  renderer.resize (1800 * lavoir::pixel, 1300 * lavoir::pixel);
  const lavoir::image image = renderer.capture (seconds * mp_units::si::second);

  const std::size_t width = image.width.numerical_value_in (lavoir::pixel);
  const std::size_t height = image.height.numerical_value_in (lavoir::pixel);
  NSURL* url = [NSURL fileURLWithPath:@(path)];
  NSData* pixels = [NSData dataWithBytesNoCopy:image.bgra.get ()
                                        length:4 * width * height
                                  freeWhenDone:NO];
  CGColorSpaceRef colour_space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
  CGDataProviderRef provider =
    CGDataProviderCreateWithCFData ((__bridge CFDataRef)pixels);
  const CGBitmapInfo bgra_little_endian =
    CGBitmapInfo (kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little;
  CGImageRef cg_image = CGImageCreate (width,
                                       height,
                                       8,
                                       32,
                                       4 * width,
                                       colour_space,
                                       bgra_little_endian,
                                       provider,
                                       nullptr,
                                       false,
                                       kCGRenderingIntentDefault);
  CGImageDestinationRef destination =
    CGImageDestinationCreateWithURL ((__bridge CFURLRef)url,
                                     (__bridge CFStringRef)UTTypePNG.identifier,
                                     1,
                                     nullptr);

  int status = -1;
  if (cg_image && destination) {
    CGImageDestinationAddImage (destination, cg_image, nullptr);
    status = CGImageDestinationFinalize (destination) ? 0 : -1;
  }
  if (destination)
    CFRelease (destination);
  if (cg_image)
    CGImageRelease (cg_image);
  CGDataProviderRelease (provider);
  CGColorSpaceRelease (colour_space);

  if (status != 0)
    std::fprintf (stderr, "lavoir: could not write %s\n", path);
  return status;
}

int main (int argc, char** argv) {
  @autoreleasepool {
    const char* capture_path = nullptr;
    double capture_seconds = 0.0;
    for (int index = 1; index < argc; ++index) {
      if (std::strcmp (argv[index], "--capture") == 0 && index + 1 < argc) {
        capture_path = argv[++index];
        if (index + 1 < argc && argv[index + 1][0] != '-')
          capture_seconds = std::strtod (argv[++index], nullptr);
      } else {
        std::fprintf (stderr, "usage: lavoir [--capture PATH [SECONDS]]\n");
        return -1;
      }
    }
    if (capture_path)
      return capture_frame (capture_path, capture_seconds);

    NSApplication* app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    LavoirApplication* delegate = [[LavoirApplication alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

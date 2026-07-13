#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "atelier/atelier_renderer.hh"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

@interface AtelierView : NSView
@end

@implementation AtelierView {
  std::unique_ptr<atelier::Renderer> _renderer;
  NSTimer* _timer;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _renderer = std::make_unique<atelier::Renderer> ();
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
  const atelier::Viewport viewport {
    .width = static_cast<std::size_t> (pixels.size.width),
    .height = static_cast<std::size_t> (pixels.size.height),
  };
  _renderer->resize (viewport);
  _renderer->draw ();
}

@end

@interface AtelierApplication : NSObject <NSApplicationDelegate>
@end

@implementation AtelierApplication {
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
  _window.title = @"Atelier";
  _window.contentView = [[AtelierView alloc] initWithFrame:frame];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  (void)app;
  return YES;
}

@end

// Photograph the scene without opening a window: render one frame a
// few seconds in and write it as a PNG.
static int capture_scene (const char* path) {
  atelier::Renderer renderer;
  renderer.resize ({ .width = 1800, .height = 1300 });
  const atelier::Image image =
    renderer.capture (atelier::Duration (4.0f * mp_units::si::second));

  NSURL* url = [NSURL fileURLWithPath:@(path)];
  NSData* pixels = [NSData dataWithBytesNoCopy:image.bgra.get ()
                                        length:4 * image.width * image.height
                                  freeWhenDone:NO];
  CGColorSpaceRef colour_space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
  CGDataProviderRef provider =
    CGDataProviderCreateWithCFData ((__bridge CFDataRef)pixels);
  const CGBitmapInfo bgra_little_endian =
    CGBitmapInfo (kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little;
  CGImageRef cg_image = CGImageCreate (image.width,
                                       image.height,
                                       8,
                                       32,
                                       4 * image.width,
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
    std::fprintf (stderr, "atelier: could not write %s\n", path);
  return status;
}

int main (int argc, char** argv) {
  @autoreleasepool {
    if (argc == 3 && std::strcmp (argv[1], "--capture") == 0)
      return capture_scene (argv[2]);

    NSApplication* app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    AtelierApplication* delegate = [[AtelierApplication alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

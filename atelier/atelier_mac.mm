#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "atelier/atelier_renderer.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

@interface AtelierView : NSView
- (instancetype)initWithFrame:(NSRect)frame scene:(atelier::SceneKind)scene;
@end

@implementation AtelierView {
  std::unique_ptr<atelier::Renderer> _renderer;
  NSTimer* _timer;
}

- (instancetype)initWithFrame:(NSRect)frame scene:(atelier::SceneKind)scene {
  self = [super initWithFrame:frame];
  if (self) {
    _renderer = std::make_unique<atelier::Renderer> (scene);
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
- (instancetype)initWithScene:(atelier::SceneKind)scene;
@end

@implementation AtelierApplication {
  NSWindow* _window;
  atelier::SceneKind _scene;
}

- (instancetype)initWithScene:(atelier::SceneKind)scene {
  self = [super init];
  if (self)
    _scene = scene;
  return self;
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
  switch (_scene) {
  case atelier::SceneKind::tree_wind:
    _window.title = @"Atelier — Wind-Bent Tree";
    break;
  case atelier::SceneKind::tree_diagram:
    _window.title = @"Atelier — Tree Diagram";
    break;
  case atelier::SceneKind::rope_bridge:
    _window.title = @"Atelier — Rope Bridge";
    break;
  case atelier::SceneKind::repeating_sheet:
    _window.title = @"Atelier — Repeating Plane";
    break;
  case atelier::SceneKind::toroidal_sheet:
    _window.title = @"Atelier — Toroidal Shell";
    break;
  }
  _window.contentView = [[AtelierView alloc] initWithFrame:frame scene:_scene];
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
static int capture_scene (const char* path,
                          atelier::Duration elapsed,
                          atelier::SceneKind scene) {
  atelier::Renderer renderer (scene);
  renderer.resize ({ .width = 1800, .height = 1300 });
  const atelier::Image image = renderer.capture (elapsed);

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
    atelier::SceneKind scene = atelier::SceneKind::tree_wind;
    const char* capture_path = nullptr;
    float capture_seconds = 4.0f;
    for (int index = 1; index < argc; ++index) {
      if (std::strcmp (argv[index], "--flat") == 0) {
        scene = atelier::SceneKind::repeating_sheet;
      } else if (std::strcmp (argv[index], "--bridge") == 0) {
        scene = atelier::SceneKind::rope_bridge;
      } else if (std::strcmp (argv[index], "--torus") == 0) {
        scene = atelier::SceneKind::toroidal_sheet;
      } else if (std::strcmp (argv[index], "--tree") == 0) {
        scene = atelier::SceneKind::tree_wind;
      } else if (std::strcmp (argv[index], "--tree-diagram") == 0) {
        scene = atelier::SceneKind::tree_diagram;
      } else if (std::strcmp (argv[index], "--capture") == 0 &&
                 index + 1 < argc) {
        capture_path = argv[++index];
        if (index + 1 < argc && argv[index + 1][0] != '-')
          capture_seconds = std::strtof (argv[++index], nullptr);
      } else {
        std::fprintf (stderr,
                      "usage: atelier [--tree|--tree-diagram|--bridge|--flat|"
                      "--torus] "
                      "[--capture PATH [SECONDS]]\n");
        return -1;
      }
    }
    if (capture_path)
      return capture_scene (
        capture_path,
        atelier::Duration (capture_seconds * mp_units::si::second),
        scene);

    NSApplication* app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    AtelierApplication* delegate =
      [[AtelierApplication alloc] initWithScene:scene];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include "atelier/atelier_renderer.hh"

#include <memory>

@interface AtelierRenderer : NSObject <MTKViewDelegate>
@end

@implementation AtelierRenderer {
  std::unique_ptr<atelier::Renderer> _renderer;
  CFTimeInterval _started_at;
}

- (instancetype)initWithView:(MTKView*)view {
  self = [super init];
  if (!self)
    return nil;

  _renderer =
    std::make_unique<atelier::Renderer> ((__bridge MTL::Device*)view.device);
  _started_at = CACurrentMediaTime ();
  return self;
}

- (void)drawInMTKView:(MTKView*)view {
  id<CAMetalDrawable> drawable = view.currentDrawable;
  MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
  if (!drawable || !pass)
    return;

  const CGSize size = view.drawableSize;
  const float aspect = static_cast<float> (size.width / size.height);
  _renderer->draw ((__bridge MTL::RenderPassDescriptor*)pass,
                   (__bridge CA::MetalDrawable*)drawable,
                   static_cast<float> (CACurrentMediaTime () - _started_at),
                   aspect);
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  (void)view;
  (void)size;
}

@end

@interface AtelierApplication : NSObject <NSApplicationDelegate>
@end

@implementation AtelierApplication {
  NSWindow* _window;
  AtelierRenderer* _renderer;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  id<MTLDevice> device = MTLCreateSystemDefaultDevice ();
  if (!device)
    @throw [NSException exceptionWithName:@"AtelierMetalError"
                                   reason:@"Metal is unavailable"
                                 userInfo:nil];

  const NSRect frame = NSMakeRect (0, 0, 900, 650);
  _window = [[NSWindow alloc]
    initWithContentRect:frame
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable |
                        NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered
                  defer:NO];
  _window.title = @"Atelier";
  [_window center];

  MTKView* view = [[MTKView alloc] initWithFrame:frame device:device];
  view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
  view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
  view.clearColor = MTLClearColorMake (0.025, 0.032, 0.05, 1);
  view.preferredFramesPerSecond = 60;
  _renderer = [[AtelierRenderer alloc] initWithView:view];
  view.delegate = _renderer;
  _window.contentView = view;
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  (void)app;
  return YES;
}

@end

int main () {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    AtelierApplication* delegate = [[AtelierApplication alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

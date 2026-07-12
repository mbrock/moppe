#import <Cocoa/Cocoa.h>

#include "atelier/atelier_renderer.hh"

#include <memory>

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
  _renderer->resize (pixels.size.width, pixels.size.height);
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

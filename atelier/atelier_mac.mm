#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include "atelier/atelier.hh"

#include <string>

@interface AtelierRenderer : NSObject <MTKViewDelegate>
@end

@implementation AtelierRenderer {
  id<MTLCommandQueue> _queue;
  id<MTLRenderPipelineState> _pipeline;
  id<MTLDepthStencilState> _depth_state;
  id<MTLBuffer> _vertices;
  NSUInteger _vertex_count;
  CFTimeInterval _started_at;
}

- (instancetype)initWithView:(MTKView*)view {
  self = [super init];
  if (!self)
    return nil;

  id<MTLDevice> device = view.device;
  _queue = [device newCommandQueue];

  NSError* error = nil;
  const std::string source (atelier::shader_source ());
  NSString* shader_source = [NSString stringWithUTF8String:source.c_str ()];
  id<MTLLibrary> library = [device newLibraryWithSource:shader_source
                                                options:nil
                                                  error:&error];
  if (!library)
    @throw [NSException exceptionWithName:@"AtelierShaderError"
                                   reason:error.localizedDescription
                                 userInfo:nil];

  MTLRenderPipelineDescriptor* descriptor =
    [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = [library newFunctionWithName:@"atelier_vertex"];
  descriptor.fragmentFunction =
    [library newFunctionWithName:@"atelier_fragment"];
  descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat;
  descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
  _pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                     error:&error];
  if (!_pipeline)
    @throw [NSException exceptionWithName:@"AtelierPipelineError"
                                   reason:error.localizedDescription
                                 userInfo:nil];

  MTLDepthStencilDescriptor* depth = [[MTLDepthStencilDescriptor alloc] init];
  depth.depthCompareFunction = MTLCompareFunctionLess;
  depth.depthWriteEnabled = YES;
  _depth_state = [device newDepthStencilStateWithDescriptor:depth];

  const std::vector<atelier::GpuVector> vertices = atelier::coin_wire_mesh ();
  _vertex_count = vertices.size ();
  _vertices = [device newBufferWithBytes:vertices.data ()
                                  length:vertices.size () * sizeof (vertices[0])
                                 options:MTLResourceStorageModeShared];
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
  const atelier::Frame frame = atelier::frame (
    static_cast<float> (CACurrentMediaTime () - _started_at), aspect);

  pass.colorAttachments[0].clearColor =
    MTLClearColorMake (0.025, 0.032, 0.05, 1);
  pass.depthAttachment.clearDepth = 1.0;

  id<MTLCommandBuffer> command = [_queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder =
    [command renderCommandEncoderWithDescriptor:pass];
  [encoder setRenderPipelineState:_pipeline];
  [encoder setDepthStencilState:_depth_state];
  [encoder setVertexBuffer:_vertices offset:0 atIndex:0];
  [encoder setVertexBytes:&frame.uniforms
                   length:sizeof (frame.uniforms)
                  atIndex:1];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:_vertex_count];
  [encoder endEncoding];
  [command presentDrawable:drawable];
  [command commit];
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

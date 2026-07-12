#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <simd/simd.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace atelier {
  using Matrix = simd_float4x4;
  using Vector = simd_float3;

  struct Uniforms {
    Matrix world_to_clip;
  };

  Matrix perspective (float vertical_fov,
                      float aspect,
                      float near_plane,
                      float far_plane) {
    const float y = 1.0f / std::tan (vertical_fov * 0.5f);
    const float x = y / aspect;
    const float z = far_plane / (near_plane - far_plane);
    return { simd_make_float4 (x, 0, 0, 0),
             simd_make_float4 (0, y, 0, 0),
             simd_make_float4 (0, 0, z, -1),
             simd_make_float4 (0, 0, near_plane * z, 0) };
  }

  Matrix look_at (Vector eye, Vector target, Vector up) {
    const Vector z = simd_normalize (eye - target);
    const Vector x = simd_normalize (simd_cross (up, z));
    const Vector y = simd_cross (z, x);
    return { simd_make_float4 (x.x, y.x, z.x, 0),
             simd_make_float4 (x.y, y.y, z.y, 0),
             simd_make_float4 (x.z, y.z, z.z, 0),
             simd_make_float4 (
               -simd_dot (x, eye), -simd_dot (y, eye), -simd_dot (z, eye), 1) };
  }

  void append_rod (std::vector<Vector>& mesh,
                   Vector start,
                   Vector end,
                   float radius) {
    const Vector direction = simd_normalize (end - start);
    const Vector reference =
      std::abs (simd_dot (direction, Vector { 0, 1, 0 })) > 0.9f
        ? Vector { 1, 0, 0 }
        : Vector { 0, 1, 0 };
    const Vector u =
      radius * simd_normalize (simd_cross (direction, reference));
    const Vector v = radius * simd_normalize (simd_cross (direction, u));
    const std::array<Vector, 8> corners { start - u - v, start + u - v,
                                          start + u + v, start - u + v,
                                          end - u - v,   end + u - v,
                                          end + u + v,   end - u + v };
    constexpr std::array<unsigned, 36> triangles { 0, 1, 5, 0, 5, 4, 1, 2, 6,
                                                   1, 6, 5, 2, 3, 7, 2, 7, 6,
                                                   3, 0, 4, 3, 4, 7, 0, 3, 2,
                                                   0, 2, 1, 4, 5, 6, 4, 6, 7 };
    for (const unsigned index : triangles)
      mesh.push_back (corners[index]);
  }

  std::vector<Vector> coin_wire_mesh () {
    constexpr int sides = 6;
    constexpr float radius = 1.0f;
    constexpr float half_thickness = 0.16f;
    constexpr float pi = 3.14159265358979323846f;
    std::array<Vector, sides> upper;
    std::array<Vector, sides> lower;
    for (int i = 0; i < sides; ++i) {
      const float angle = 2.0f * pi * static_cast<float> (i) / sides;
      const float x = radius * std::cos (angle);
      const float z = radius * std::sin (angle);
      upper[i] = { x, half_thickness, z };
      lower[i] = { x, -half_thickness, z };
    }

    std::vector<Vector> edges;
    edges.reserve (sides * 6);
    for (int i = 0; i < sides; ++i) {
      const int next = (i + 1) % sides;
      edges.push_back (upper[i]);
      edges.push_back (upper[next]);
      edges.push_back (lower[i]);
      edges.push_back (lower[next]);
      edges.push_back (upper[i]);
      edges.push_back (lower[i]);
    }

    std::vector<Vector> mesh;
    mesh.reserve (edges.size () * 18);
    for (std::size_t i = 0; i < edges.size (); i += 2)
      append_rod (mesh, edges[i], edges[i + 1], 0.025f);
    return mesh;
  }

  NSString* shader_source () {
    return [NSString stringWithUTF8String:R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 world_to_clip;
};

vertex float4 atelier_vertex(const device float3* positions [[buffer(0)]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             uint id [[vertex_id]]) {
  return uniforms.world_to_clip * float4(positions[id], 1.0);
}

fragment half4 atelier_fragment() {
  return half4(0.94h, 0.72h, 0.27h, 1.0h);
}
)"];
  }
}

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
  id<MTLLibrary> library =
    [device newLibraryWithSource:atelier::shader_source ()
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

  const std::vector<atelier::Vector> vertices = atelier::coin_wire_mesh ();
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

  const float seconds =
    static_cast<float> (CACurrentMediaTime () - _started_at);
  const float angle = seconds * 0.32f;
  const atelier::Vector eye = { 3.0f * std::sin (angle),
                                1.55f,
                                3.0f * std::cos (angle) };
  const CGSize size = view.drawableSize;
  const float aspect = static_cast<float> (size.width / size.height);
  const atelier::Matrix projection =
    atelier::perspective (0.78f, aspect, 0.05f, 50.0f);
  const atelier::Matrix view_matrix =
    atelier::look_at (eye, { 0, 0, 0 }, { 0, 1, 0 });
  const atelier::Uniforms uniforms { .world_to_clip =
                                       simd_mul (projection, view_matrix) };

  pass.colorAttachments[0].clearColor =
    MTLClearColorMake (0.025, 0.032, 0.05, 1);
  pass.depthAttachment.clearDepth = 1.0;

  id<MTLCommandBuffer> command = [_queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder =
    [command renderCommandEncoderWithDescriptor:pass];
  [encoder setRenderPipelineState:_pipeline];
  [encoder setDepthStencilState:_depth_state];
  [encoder setVertexBuffer:_vertices offset:0 atIndex:0];
  [encoder setVertexBytes:&uniforms length:sizeof (uniforms) atIndex:1];
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

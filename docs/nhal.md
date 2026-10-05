# NHAL: the next renderer's hardware layer

NHAL is a small C++ hardware layer for moppe's next renderer, in the spirit
of Luv's WebGPU-shaped HAL but shaped by what the targets actually offer. It
has two backends: Metal 4 on Apple platforms and Direct3D 12 for Xbox Series
consoles in Developer Mode (UWP, feature level 11_0, shader model 6.4). Vulkan
can follow. It lives in `moppe/nhal/`, beside the current renderer, which it
does not touch until it can carry a moppe-like scene on both platforms.

Shaders are written in Luv's mathematical shader language and lowered ahead of
time by `luv-shaderc` (a package of the Luv flake) into MSL and HLSL, plus the
reflection NHAL builds its pipelines from.

## What the Xbox allows

Measured by nixbox's `probes/d3d12-caps` and `probes/swapchain`:

- Shader model 6.4, compiled by DXC on the build host (it signs DXIL). No 6.5
  or 6.6, so no `ResourceDescriptorHeap` indexing and no buffer pointers in
  shaders. No mesh shaders, ray tracing, enhanced barriers, or 16-bit math.
- Resource binding tier 3: root descriptors by GPU address, unbounded SRV
  tables, a 1,000,000-descriptor shader-visible heap, `ExecuteIndirect` with
  root constants. Waves are 64 lanes.
- Every scene format renders, blends, multisamples, and resolves.
- A 3840x2160 swapchain for the 1080p CoreWindow reaches the TV natively;
  HDR10 and scRGB swapchains are accepted.

## The binding contract

A program is a set of stages (vertex + fragment, or compute) whose resources
are linked by name. Every resource has a kind and a binding number. Binding
numbers are per kind family, and the families map onto each backend like this:

| Luv resource | Family | Metal 4 | Direct3D 12 HLSL | D3D12 root signature |
| --- | --- | --- | --- | --- |
| `:uniform-block` | buffer | `constant B& n [[buffer(i)]]` | `cbuffer n : register(b i)` | root CBV |
| `:storage-buffer` (read) | buffer | `device const T* n [[buffer(i)]]` | `StructuredBuffer<T> n : register(t i, space0)` | root SRV |
| `:storage-buffer :access :read-write` | buffer | `device T* n [[buffer(i)]]` | `RWStructuredBuffer<T> n : register(u i)` | root UAV |
| `:texture-2d`, `:depth-texture-2d`, `:uint-texture-2d` | texture | `texture2d<…> n [[texture(i)]]` | `Texture2D<…> n : register(t i, space1)` | one descriptor table, t0–t15 space1 |
| `:texture-2d-array`, `:depth-texture-2d-array`, `:texture-cube`, `:texture-3d` | texture | `texture2d_array<…>`, `depth2d_array<float>`, `texturecube<…>`, `texture3d<…>` at `[[texture(i)]]` | `Texture2DArray`, `TextureCube`, `Texture3D` at `register(t i, space1)` | same table |
| `:read-write-texture-2d` | storage texture | `texture2d<…, access::read_write> n [[texture(16 + i)]]` | `RWTexture2D<…> n : register(u i, space1)` | a UAV table, u0–u15 space1 |
| `:sampler` | sampler | `sampler n [[sampler(i)]]` | `SamplerState` or `SamplerComparisonState n : register(s i)` | static samplers |

- Buffer binding numbers are unique across uniform blocks and storage buffers
  (Metal shares one buffer index space); 0–15.
- Texture binding numbers are 0–15, and so are storage texture binding
  numbers, which are a separate family. NHAL copies each draw's textures into a
  contiguous range of the shader-visible heap; true bindless (an unbounded
  table indexed by the shader) is a later, separate resource kind.
- Samplers are a fixed standard set, chosen by binding number. A sampler used
  with `sample-compare` is a comparison sampler in HLSL.

  | Binding | Sampler |
  | --- | --- |
  | 0 | linear filtering, clamp to edge |
  | 1 | linear filtering, repeat |
  | 2 | nearest, clamp to edge |
  | 3 | linear comparison (`less-equal`), clamp to edge |

- The descriptor `:set` is 0 or absent.
- Uniform blocks hold only `vec4` lanes at 16-byte offsets, so the C++ struct,
  MSL `constant`, and HLSL `cbuffer` layouts agree without packing rules.
- There are no vertex buffers or input layouts: vertex stages pull from
  storage buffers with the `:vertex-index` and `:instance-index` built-ins.
  Index buffers are allowed.

- Storage buffer elements have 1, 2, or 4 components (no `vec3`), so strides
  agree between Metal and HLSL.

Inter-stage values: a vertex output or fragment input at `:location n` is HLSL
semantic `LOCATIONn` and MSL `[[user(locnN)]]`; the `:position` built-in is
`SV_Position`; fragment output `:location n` is `SV_Target n`. The HLSL
fragment input signature is always `SV_Position` followed by every vertex
output, since Direct3D matches stages by layout.

Clip space: shaders write `:position` in the language's convention, y down
(Vulkan's), and both lowerings negate y, so what reaches Metal and Direct3D
is their y up with depth 0..1. Renderers use reversed-Z.

## What `luv-shaderc` produces

`nix run .#luv-shaderc -- --out DIR [--target msl] [--target hlsl] FILE.lisp`
(this repository's flake pins the Luv version). Source files are plain
`define-shader`, `define-shader-function`, and
`(define-shader-program NAME :vertex V :fragment F)` (or `:compute C`) forms
in the `luv.shader-user` package. Entry points are `NAME_STAGE`; file names
and namespaces are the program name in snake_case. For each program, into the
output directory:

- `NAME.STAGE.metal`, one MSL document per stage.
- `NAME.STAGE.hlsl`, one HLSL document per stage, compiled by DXC with
  `-T vs_6_0` / `ps_6_0` / `cs_6_0` (or higher, up to 6.4) and the entry point
  named in the manifest.
- `NAME.json`, the reflection manifest: stages and entry points, every
  resource (name, kind, binding, stages, byte size, uniform members and
  offsets, storage element type), and the fragment outputs.
- `NAME.hh`, the same reflection as C++ for NHAL, and one struct per uniform
  block whose members are `std::array<float, 4>` lanes named after the
  shader's members, with a `static_assert` on its size:

```cpp
// Generated by luv-shaderc from terrain.lisp; do not edit.
#pragma once
#include <moppe/nhal/reflection.hh>
#include <array>

namespace moppe::nhal::shaders::terrain {
  struct FrameState {
    std::array<float, 4> camera;
    std::array<float, 4> sun_direction;
  };
  static_assert(sizeof(FrameState) == 32);

  inline constexpr Resource resources[] = {
    {"frame_state", ResourceKind::uniform_block, 0,
     stage_vertex | stage_fragment, sizeof(FrameState)},
    {"heights", ResourceKind::storage_buffer, 1, stage_vertex, 0},
    {"albedo", ResourceKind::texture_2d, 0, stage_fragment, 0},
    {"linear_clamp", ResourceKind::sampler, 0, stage_fragment, 0},
  };
  inline constexpr Program program {
    .name = "terrain",
    .vertex_entry = "terrain_vertex",
    .fragment_entry = "terrain_fragment",
    .compute_entry = nullptr,
    .resources = resources,
    .color_outputs = 1,
  };
}
```

`moppe/nhal/reflection.hh` defines `Resource`, `ResourceKind`, the stage bits,
and `Program`; it is the C++ half of this contract.

// The demo's shaders for Direct3D 12, the same program as scene.metal in
// the NHAL binding contract (docs/nhal.md): the frame block is b0, the
// pulled storage buffer t1 in space 0, the tonemap's input t0 in space 1
// with the standard linear clamp sampler s0.

cbuffer frame_state : register(b0)
{
    float4 camera_position; // w: seconds
    float4 camera_right;    // right / tan(fov x / 2); w: tan
    float4 camera_up;       // up / tan(fov y / 2); w: tan
    float4 camera_forward;  // w: near plane
    float4 sun_direction;
    float4 sun_color;
    float4 sky_zenith;
    float4 sky_horizon;     // w: fog density
    float4 terrain;         // cell, samples per side, x0, z0
};

StructuredBuffer<float4> pulled : register(t1, space0);
Texture2D<float4> scene : register(t0, space1);
SamplerState linear_clamp : register(s0);

static const float pi = 3.14159265f;

// Reversed-Z with an infinite far plane: depth is near / view distance.
float4 project(float3 world)
{
    const float3 rel = world - camera_position.xyz;
    return float4(dot(rel, camera_right.xyz), dot(rel, camera_up.xyz),
                  camera_forward.w, dot(rel, camera_forward.xyz));
}

float3 sky(float3 ray)
{
    const float up = saturate(ray.y);
    float3 color = lerp(sky_horizon.xyz, sky_zenith.xyz, pow(up, 0.6f));
    const float sun = max(dot(ray, sun_direction.xyz), 0.0f);
    color += sun_color.xyz * (pow(sun, 900.0f) * 6.0f + pow(sun, 12.0f) * 0.08f);
    return color;
}

float3 shade(float3 albedo, float3 normal, float3 world)
{
    const float diffuse = max(dot(normal, sun_direction.xyz), 0.0f);
    const float3 ambient = lerp(sky_horizon.xyz * 0.35f, sky_zenith.xyz,
                                normal.y * 0.5f + 0.5f) * 0.55f;
    const float3 lit = albedo * (sun_color.xyz * diffuse + ambient);
    const float3 rel = world - camera_position.xyz;
    const float distance = length(rel);
    const float fog = 1.0f - exp(-distance * sky_horizon.w);
    return lerp(lit, sky(rel / distance), fog);
}

struct Surface
{
    float4 position : SV_Position;
    float3 world : LOCATION0;
    float3 normal : LOCATION1;
    float3 albedo : LOCATION2;
};

// -- terrain -------------------------------------------------------------

Surface terrain_vertex(uint vertex_id : SV_VertexID)
{
    const uint side = uint(terrain.y);
    const float4 sample = pulled[vertex_id];
    const float3 world = float3(terrain.z + float(vertex_id % side) * terrain.x,
                                sample.x,
                                terrain.w + float(vertex_id / side) * terrain.x);
    Surface o;
    o.position = project(world);
    o.world = world;
    o.normal = sample.yzw;
    o.albedo = 0;
    return o;
}

float lattice(float2 p)
{
    return frac(sin(dot(floor(p), float2(127.1f, 311.7f))) * 43758.5453f);
}

float4 terrain_fragment(Surface i) : SV_Target0
{
    const float3 normal = normalize(i.normal);
    const float patch = lattice(i.world.xz / 9.0f) * 0.5f
                        + lattice(i.world.xz / 2.3f) * 0.5f;
    const float3 grass = lerp(float3(0.10f, 0.16f, 0.05f),
                              float3(0.20f, 0.22f, 0.09f), patch);
    const float3 rock = float3(0.27f, 0.25f, 0.23f) * (0.8f + 0.4f * patch);
    const float steep = smoothstep(0.78f, 0.62f, normal.y);
    return float4(shade(lerp(grass, rock, steep), normal, i.world), 1);
}

// -- trees ---------------------------------------------------------------

Surface trees_vertex(uint vertex_id : SV_VertexID,
                     uint instance_id : SV_InstanceID)
{
    const float4 root = pulled[instance_id * 2];
    const float4 shape = pulled[instance_id * 2 + 1];
    const float height = root.w;
    const float crown_base = shape.z * height;
    float3 local, normal, albedo;
    if (vertex_id < 18) {
        const uint ring = vertex_id / 9;
        const float angle = float(vertex_id % 9) * (pi / 4.0f);
        const float2 around = float2(cos(angle), sin(angle));
        const float radius = shape.x * (ring == 0 ? 1.0f : 0.6f);
        local = float3(around.x * radius, ring == 0 ? -0.5f : crown_base + 0.5f,
                       around.y * radius);
        normal = float3(around.x, 0, around.y);
        albedo = float3(0.13f, 0.09f, 0.06f);
    } else {
        const uint k = vertex_id - 18;
        const float tier = float(k / 10);
        const float crown = height - crown_base;
        const float base_y = crown_base + crown * tier * 0.27f;
        const float apex_y = base_y + crown * 0.48f;
        const float radius = shape.y * (1.0f - tier * 0.27f);
        if (k % 10 == 9) {
            local = float3(0, apex_y, 0);
            normal = float3(0, 1, 0);
        } else {
            const float angle = float(k % 10) * (pi / 4.0f) + tier * 0.4f
                                + shape.w * 6.28f;
            const float2 around = float2(cos(angle), sin(angle));
            local = float3(around.x * radius, base_y, around.y * radius);
            normal = normalize(float3(around.x * (apex_y - base_y), radius,
                                      around.y * (apex_y - base_y)));
        }
        albedo = lerp(float3(0.035f, 0.07f, 0.04f), float3(0.06f, 0.10f, 0.045f),
                      shape.w);
    }
    const float3 world = root.xyz + local;
    Surface o;
    o.position = project(world);
    o.world = world;
    o.normal = normal;
    o.albedo = albedo;
    return o;
}

float4 trees_fragment(Surface i) : SV_Target0
{
    return float4(shade(i.albedo, normalize(i.normal), i.world), 1);
}

// -- sky and tonemap -----------------------------------------------------

struct Screen
{
    float4 position : SV_Position;
    float2 ndc : LOCATION0;
};

// One triangle over the screen, at the far plane (reversed-Z zero).
Screen fullscreen(uint vertex_id)
{
    const float2 ndc = float2(vertex_id == 1 ? 3.0f : -1.0f,
                              vertex_id == 2 ? 3.0f : -1.0f);
    Screen o;
    o.position = float4(ndc, 0, 1);
    o.ndc = ndc;
    return o;
}

Screen sky_vertex(uint vertex_id : SV_VertexID)
{
    return fullscreen(vertex_id);
}

float4 sky_fragment(Screen i) : SV_Target0
{
    const float tan_x = camera_right.w, tan_y = camera_up.w;
    const float3 ray = normalize(camera_forward.xyz
                                 + camera_right.xyz * (i.ndc.x * tan_x * tan_x)
                                 + camera_up.xyz * (i.ndc.y * tan_y * tan_y));
    return float4(sky(ray), 1);
}

Screen tonemap_vertex(uint vertex_id : SV_VertexID)
{
    return fullscreen(vertex_id);
}

float4 tonemap_fragment(Screen i) : SV_Target0
{
    const float2 uv = float2(i.ndc.x * 0.5f + 0.5f, 0.5f - i.ndc.y * 0.5f);
    const float3 x = scene.Sample(linear_clamp, uv).rgb * 0.9f;
    // Narkowicz's ACES fit, then the display's gamma.
    const float3 mapped = saturate((x * (2.51f * x + 0.03f))
                                   / (x * (2.43f * x + 0.59f) + 0.14f));
    return float4(pow(mapped, 1.0f / 2.2f), 1);
}

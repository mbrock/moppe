#include <metal_stdlib>
using namespace metal;

constant uint none = 0xffffffffu;
constant uint group_size = 128;
struct Params {
  uint width, height, level;
  float dx, dz, dt, uplift_dt, sea;
  float incision, reference_area, exponent, channel_area;
  float concentration, max_deposit, diffusion, critical, multiplier;
};
struct Bucket {
  atomic_uint count;
  uint groups, y, z;
};
struct Node {
  uint left, right, sum;
};
// Metres of cover, cumulative detachment, deposition, and tectonic rise.
struct Material {
  float cover, cut, fill, rise;
};
struct Budget {
  float exported;
  uint drained;
};

uint neighbor (uint i, int dx, int dz, constant Params& p) {
  int x = int (i % p.width) + dx, z = int (i / p.width) + dz;
  if (x < 0)
    x += p.width;
  if (z < 0)
    z += p.height;
  if (x == int (p.width))
    x = 0;
  if (z == int (p.height))
    z = 0;
  return uint (z) * p.width + uint (x);
}
constant int2 around[] = { int2 (1, 0),  int2 (1, 1),  int2 (0, 1),
                           int2 (-1, 1), int2 (-1, 0), int2 (-1, -1),
                           int2 (0, -1), int2 (1, -1) };
uint adjacent (uint i, uint k, constant Params& p) {
  return neighbor (i, around[k].x, around[k].y, p);
}
float distance_to (uint i, uint j, constant Params& p) {
  if (i / p.width == j / p.width)
    return p.dx;
  if (i % p.width == j % p.width)
    return p.dz;
  return length (float2 (p.dx, p.dz));
}
// One reservation per SIMD group. All lanes reach the collective.
void append (bool keep, uint item, device uint* list, device Bucket& b) {
  uint n = simd_sum (uint (keep)), offset = 0;
  if (simd_is_first () && n)
    offset = atomic_fetch_add_explicit (&b.count, n, memory_order_relaxed);
  offset =
    simd_broadcast_first (offset) + simd_prefix_exclusive_sum (uint (keep));
  if (keep)
    list[offset] = item;
}
kernel void clear_buckets (constant Params& p [[buffer (0)]],
                           device Bucket* b [[buffer (1)]],
                           uint i [[thread_position_in_grid]]) {
  atomic_store_explicit (&b[i].count, 0, memory_order_relaxed);
  b[i].groups = 0;
  b[i].y = b[i].z = 1;
}
kernel void grids (constant Params& p [[buffer (0)]],
                   device Bucket* b [[buffer (1)]],
                   uint i [[thread_position_in_grid]]) {
  uint n = atomic_load_explicit (&b[i].count, memory_order_relaxed);
  b[i].groups = (n + group_size - 1) / group_size;
}
kernel void receivers (constant Params& p [[buffer (0)]],
                       device const float* h [[buffer (1)]],
                       device const uchar* ocean [[buffer (2)]],
                       device uint* parent [[buffer (3)]],
                       device uint* component [[buffer (4)]],
                       device uint* minima [[buffer (5)]],
                       device Bucket* buckets [[buffer (6)]],
                       device Budget* budget [[buffer (7)]],
                       uint i [[thread_position_in_grid]]) {
  budget[i].drained = ocean[i] ? 1 : 0;
  uint r = i;
  float best = 0;
  if (!ocean[i]) {
    for (uint k = 0; k < 8; ++k) {
      uint j = adjacent (i, k, p);
      float grade = (h[i] - h[j]) / distance_to (i, j, p);
      if (grade > best || (grade == best && r != i && j < r)) {
        r = j;
        best = grade;
      }
    }
  }
  parent[i] = r;
  component[i] = ocean[i] ? none : i;
  append (r == i && !ocean[i], i, minima, buckets[0]);
}
// Eight pointer jumps per pass on the immutable previous snapshot.
kernel void roots (constant Params& p [[buffer (0)]],
                   device const uint* src [[buffer (1)]],
                   device uint* dst [[buffer (2)]],
                   uint i [[thread_position_in_grid]]) {
  uint j = src[i];
  for (uint k = 1; k < 8; ++k) {
    uint next = src[j];
    if (j == next)
      break;
    j = next;
  }
  dst[i] = j;
}
kernel void borders (constant Params& p [[buffer (0)]],
                     device const uint* roots [[buffer (1)]],
                     device const uchar* ocean [[buffer (2)]],
                     device uint* basin [[buffer (3)]],
                     device uint* border [[buffer (4)]],
                     device Bucket* buckets [[buffer (5)]],
                     uint i [[thread_position_in_grid]]) {
  uint a = roots[i], b = roots[neighbor (i, 1, 0, p)],
       c = roots[neighbor (i, 0, 1, p)];
  a = ocean[a] ? none : a;
  b = ocean[b] ? none : b;
  c = ocean[c] ? none : c;
  basin[i] = a;
  append (a != b || a != c, i, border, buckets[1]);
}
uint endpoint (uint edge, constant Params& p) {
  return (edge & 1) ? neighbor (edge / 2, 0, 1, p)
                    : neighbor (edge / 2, 1, 0, p);
}
uint label (uint basin, device const uint* component) {
  return basin == none ? none : component[basin];
}
// A unique undirected edge key breaks equal-saddle ties on flat ground.
// CAS compares the full elevation and edge id without 64-bit GPU atomics.
void offer (uint root,
            uint edge,
            float height,
            device atomic_uint* best,
            device const float* h,
            constant Params& p) {
  if (root == none)
    return;
  uint old = atomic_load_explicit (best + root, memory_order_relaxed);
  for (;;) {
    if (old == edge)
      return;
    if (old != none) {
      float other = max (h[old / 2], h[endpoint (old, p)]);
      if (other < height || (other == height && old < edge))
        return;
    }
    if (atomic_compare_exchange_weak_explicit (
          best + root, &old, edge, memory_order_relaxed, memory_order_relaxed))
      return;
  }
}
kernel void saddles (constant Params& p [[buffer (0)]],
                     device const float* h [[buffer (1)]],
                     device const uint* basin [[buffer (2)]],
                     device const uint* comp [[buffer (3)]],
                     device atomic_uint* best [[buffer (4)]],
                     device const uint* border [[buffer (5)]],
                     device Bucket* buckets [[buffer (6)]],
                     uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&buckets[1].count, memory_order_relaxed))
    return;
  uint i = border[t], a = label (basin[i], comp);
  for (uint k = 0; k < 2; ++k) {
    uint edge = 2 * i + k, j = endpoint (edge, p), b = label (basin[j], comp);
    if (a != b) {
      float height = max (h[i], h[j]);
      offer (a, edge, height, best, h, p);
      offer (b, edge, height, best, h, p);
    }
  }
}
kernel void reset_choices (constant Params& p [[buffer (0)]],
                           device const uint* minima [[buffer (1)]],
                           device uint* best [[buffer (2)]],
                           device Bucket* b [[buffer (3)]],
                           uint t [[thread_position_in_grid]]) {
  if (t < atomic_load_explicit (&b[0].count, memory_order_relaxed))
    best[minima[t]] = none;
}
// Reverse the path within this component from its saddle to its pit.
// Concurrent reversals are disjoint, including at a two-way saddle.
kernel void carve (constant Params& p [[buffer (0)]],
                   device const uint* minima [[buffer (1)]],
                   device const uint* best [[buffer (2)]],
                   device const uint* basin [[buffer (3)]],
                   device const uint* comp [[buffer (4)]],
                   device uint* merge [[buffer (5)]],
                   device uint* parent [[buffer (6)]],
                   device Bucket* b [[buffer (7)]],
                   uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&b[0].count, memory_order_relaxed))
    return;
  uint root = minima[t];
  if (comp[root] != root) {
    merge[root] = comp[root];
    return;
  }
  uint edge = best[root];
  merge[root] = root;
  if (edge == none)
    return;
  uint i = edge / 2, j = endpoint (edge, p);
  if (label (basin[i], comp) != root) {
    uint tmp = i;
    i = j;
    j = tmp;
  }
  uint dest = label (basin[j], comp);
  if (dest != none && best[dest] == edge && root < dest)
    return;
  merge[root] = dest;
  while (i != root) {
    uint next = parent[i];
    parent[i] = j;
    j = i;
    i = next;
  }
  parent[root] = j;
}
kernel void jump_components (constant Params& p [[buffer (0)]],
                             device const uint* minima [[buffer (1)]],
                             device const uint* src [[buffer (2)]],
                             device uint* dst [[buffer (3)]],
                             device Bucket* b [[buffer (4)]],
                             uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&b[0].count, memory_order_relaxed))
    return;
  uint i = minima[t], j = src[i];
  for (uint k = 1; k < 8 && j != none; ++k) {
    uint next = src[j];
    if (next == j)
      break;
    j = next;
  }
  dst[i] = j;
}
kernel void relabel (constant Params& p [[buffer (0)]],
                     device const uint* minima [[buffer (1)]],
                     device const uint* merge [[buffer (2)]],
                     device uint* comp [[buffer (3)]],
                     device Bucket* b [[buffer (4)]],
                     uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&b[0].count, memory_order_relaxed))
    return;
  uint i = minima[t], r = merge[i];
  comp[i] = r;
  if (r != none)
    atomic_fetch_add_explicit (&b[2].count, 1, memory_order_relaxed);
}
kernel void next_merge (constant Params& p [[buffer (0)]],
                        device Bucket* b [[buffer (1)]],
                        uint i [[thread_position_in_grid]]) {
  if (!atomic_exchange_explicit (&b[2].count, 0, memory_order_relaxed)) {
    b[0].groups = 0;
    b[1].groups = 0;
  }
}

// First-child / next-sibling turns the donor forest into a binary tree.
// Its subtree sum includes the siblings. The physical catchment of i is
// one cell plus the binary subtree rooted at its first child.
kernel void donors (constant Params& p [[buffer (0)]],
                    device const uint* parent [[buffer (1)]],
                    device uint* first [[buffer (2)]],
                    device uint* sibling [[buffer (3)]],
                    uint i [[thread_position_in_grid]]) {
  uint head = none;
  for (uint k = 0; k < 8; ++k) {
    uint j = adjacent (i, k, p);
    if (j != i && parent[j] == i) {
      sibling[j] = head;
      head = j;
    }
  }
  first[i] = head;
  if (parent[i] == i)
    sibling[i] = none;
}
kernel void start_rake (constant Params& p [[buffer (0)]],
                        device const uint* first [[buffer (1)]],
                        device const uint* sibling [[buffer (2)]],
                        device Node* nodes [[buffer (3)]],
                        device Node* mirror [[buffer (4)]],
                        device uint* active [[buffer (5)]],
                        device Bucket* b [[buffer (6)]],
                        uint i [[thread_position_in_grid]]) {
  Node node = { first[i], sibling[i], 1 };
  nodes[i] = mirror[i] = node;
  append (node.left != none || node.right != none, i, active, b[0]);
}
void compress (thread uint& link, thread uint& sum, device const Node* nodes) {
  if (link == none)
    return;
  Node n = nodes[link];
  if (n.left != none && n.right != none)
    return;
  sum += n.sum;
  link = n.left == none ? n.right : n.left;
}
kernel void rake (constant Params& p [[buffer (0)]],
                  device const Node* src [[buffer (1)]],
                  device Node* dst [[buffer (2)]],
                  device const uint* active [[buffer (3)]],
                  device uint* next [[buffer (4)]],
                  device Bucket* from [[buffer (5)]],
                  device Bucket* to [[buffer (6)]],
                  uint t [[thread_position_in_grid]]) {
  bool live = t < atomic_load_explicit (&from->count, memory_order_relaxed);
  uint i = 0;
  bool keep = false;
  if (live) {
    i = active[t];
    Node n = src[i];
    keep = n.left != none || n.right != none;
    compress (n.left, n.sum, src);
    compress (n.right, n.sum, src);
    dst[i] = n;
    // One final visit leaves the completed value in both snapshots.
  }
  append (keep, i, next, *to);
}
uint order (uint area) {
  return 31 - clz (area);
}
kernel void catchments (constant Params& p [[buffer (0)]],
                        device const uint* first [[buffer (1)]],
                        device const Node* n [[buffer (2)]],
                        device uint* area [[buffer (3)]],
                        uint i [[thread_position_in_grid]]) {
  uint child = first[i];
  area[i] = child == none ? 1 : 1 + n[child].sum;
}
// At most one donor has the recipient's log-area order. Equal-order
// reaches are disjoint chains. Count the chains, then bin them by order.
kernel void chains (constant Params& p [[buffer (0)]],
                    device const uint* parent [[buffer (1)]],
                    device const uint* area [[buffer (2)]],
                    device const uint* first [[buffer (3)]],
                    device const uint* sibling [[buffer (4)]],
                    device uint* upstream [[buffer (5)]],
                    device Bucket* b [[buffer (6)]],
                    device const uchar* ocean [[buffer (7)]],
                    device atomic_uint* invalid [[buffer (8)]],
                    uint i [[thread_position_in_grid]]) {
  uint level = order (area[i]), child = none, sum = 1;
  for (uint j = first[i]; j != none; j = sibling[j]) {
    sum += area[j];
    if (order (area[j]) == level)
      child = j;
  }
  if (sum != area[i] || (parent[i] == i && !ocean[i]))
    atomic_store_explicit (invalid, 1, memory_order_relaxed);
  upstream[i] = child;
  if (child == none && (parent[i] != i || first[i] != none))
    atomic_fetch_add_explicit (&b[level].count, 1, memory_order_relaxed);
}
kernel void offsets (constant Params& p [[buffer (0)]],
                     device Bucket* b [[buffer (1)]],
                     device uint* start [[buffer (2)]],
                     device uint* cursor [[buffer (3)]],
                     uint i [[thread_position_in_grid]]) {
  uint sum = 0;
  for (uint k = 0; k < i; ++k)
    sum += atomic_load_explicit (&b[k].count, memory_order_relaxed);
  start[i] = cursor[i] = sum;
}
kernel void bin_chains (constant Params& p [[buffer (0)]],
                        device const uint* parent [[buffer (1)]],
                        device const uint* area [[buffer (2)]],
                        device const uint* first [[buffer (3)]],
                        device const uint* upstream [[buffer (4)]],
                        device atomic_uint* cursor [[buffer (5)]],
                        device uint2* ends [[buffer (6)]],
                        uint i [[thread_position_in_grid]]) {
  if (upstream[i] != none || (parent[i] == i && first[i] == none))
    return;
  uint level = order (area[i]), tail = i;
  while (parent[tail] != tail && order (area[parent[tail]]) == level)
    tail = parent[tail];
  uint slot =
    atomic_fetch_add_explicit (cursor + level, 1, memory_order_relaxed);
  ends[slot] = uint2 (i, tail);
}
// Solve backward Euler downstream-first. Store capacity and detachment
// in metres of sediment over one cell (all cells have equal area).
kernel void incise (constant Params& p [[buffer (0)]],
                    device const uint* parent [[buffer (1)]],
                    device const uint* area [[buffer (2)]],
                    device const uint* up [[buffer (3)]],
                    device const uint2* ends [[buffer (4)]],
                    device const uint* start [[buffer (5)]],
                    device Bucket* b [[buffer (6)]],
                    device const float* h [[buffer (7)]],
                    device const float* uplift [[buffer (8)]],
                    device float* solved [[buffer (9)]],
                    device float2* load [[buffer (10)]],
                    device float* spill [[buffer (11)]],
                    uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&b[p.level].count, memory_order_relaxed))
    return;
  uint i = ends[start[p.level] + t].y;
  while (i != none) {
    uint j = parent[i];
    if (i == j) {
      spill[i] = max (h[i], p.sea);
      solved[i] = h[i];
      load[i] = 0;
    } else {
      // The highest bed on the carved path is this cell's spill level.
      spill[i] = max (h[i], spill[j]);
      float raised = h[i] + p.uplift_dt * uplift[i];
      float run = distance_to (i, j, p), a = area[i] * p.dx * p.dz;
      float share = smoothstep (-1.f, 1.f, log2 (a / p.channel_area));
      float w = p.dt * p.incision * share *
                pow (a / p.reference_area, p.exponent) / run;
      float drop = max (0.f, raised - solved[j]);
      float cut = drop * (w / (1 + w));
      solved[i] = raised - cut;
      load[i] =
        float2 (cut, p.concentration * p.dt * area[i] * share * drop / run);
    }
    i = up[i];
  }
}
kernel void transport (constant Params& p [[buffer (0)]],
                       device const uint* parent [[buffer (1)]],
                       device const uint* first [[buffer (2)]],
                       device const uint* sibling [[buffer (3)]],
                       device const uint2* ends [[buffer (4)]],
                       device const uint* start [[buffer (5)]],
                       device Bucket* b [[buffer (6)]],
                       device const float2* load [[buffer (7)]],
                       device float* outgoing [[buffer (8)]],
                       device float* h [[buffer (9)]],
                       device const float* uplift [[buffer (10)]],
                       device Material* material [[buffer (11)]],
                       device Budget* budget [[buffer (12)]],
                       device const uint* area [[buffer (13)]],
                       device const float* spill [[buffer (14)]],
                       uint t [[thread_position_in_grid]]) {
  if (t >= atomic_load_explicit (&b[p.level].count, memory_order_relaxed))
    return;
  uint2 chain = ends[start[p.level] + t];
  for (uint i = chain.x;; i = parent[i]) {
    float incoming = 0;
    for (uint j = first[i]; j != none; j = sibling[j])
      incoming += outgoing[j];
    if (i == parent[i]) {
      float fill = min (incoming, min (p.max_deposit, spill[i] - h[i]));
      h[i] += fill;
      material[i].fill += fill;
      material[i].cover += fill;
      budget[i].exported += incoming - fill;
      budget[i].drained = area[i];
    } else {
      // In standing water the bed accommodates sediment up to its spill
      // level. Excess passes across the saddle to the downstream basin.
      float depth = spill[i] - h[i];
      float capacity = depth > 0 ? 0 : load[i].y;
      float cut =
        depth > 0 ? 0 : min (load[i].x, max (0.f, capacity - incoming));
      float room = depth > 0 ? min (depth, p.max_deposit) : p.max_deposit;
      float fill = min (room, max (0.f, incoming - capacity));
      float rise = p.uplift_dt * uplift[i];
      h[i] += rise - cut + fill;
      Material m = material[i];
      m.cover = max (0.f, m.cover - cut) + fill;
      m.cut += cut;
      m.fill += fill;
      m.rise += rise;
      material[i] = m;
      outgoing[i] = incoming + cut - fill;
    }
    if (i == chain.y)
      break;
  }
}
// Each pair evaluates the same signed face flux from the old surface.
// The explicit step is subcycled for the largest allowed diffusivity.
kernel void creep (constant Params& p [[buffer (0)]],
                   device const float* h [[buffer (1)]],
                   device float* dst [[buffer (2)]],
                   device const uchar* ocean [[buffer (3)]],
                   device Material* material [[buffer (4)]],
                   uint i [[thread_position_in_grid]]) {
  float cut = 0, fill = 0;
  if (!ocean[i])
    for (uint k = 0; k < 8; k += 2) {
      uint j = adjacent (i, k, p);
      if (ocean[j])
        continue;
      float d = (k == 0 || k == 4) ? p.dx : p.dz;
      float dh = h[j] - h[i];
      float gain = mix (
        1.f, p.multiplier, smoothstep (.5f, 1.f, abs (dh) / (d * p.critical)));
      float change = p.diffusion * gain * dh / (d * d);
      cut += max (0.f, -change);
      fill += max (0.f, change);
    }
  dst[i] = h[i] + fill - cut;
  Material m = material[i];
  m.cover = max (0.f, m.cover - cut) + fill;
  m.cut += cut;
  m.fill += fill;
  material[i] = m;
}

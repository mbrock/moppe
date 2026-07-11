# Graphics feature Boolean cube

## Method

On 2026-07-12, two complete 256-configuration hot-feature sweeps were run on
the Apple M2 Pro development machine at 1280 x 800 drawable pixels. Each node
restored the same deterministic `GameState`, reset renderer temporal history,
discarded 30 frames, and recorded Metal command-buffer GPU time for the next
120 fixed 120 Hz logical frames. The world used seed 123 and the Fast terrain
profile.

The eight cube dimensions were grass, ocean, particles, vehicle effects, star
effects, bloom, automatic exposure, and lens flare. One cube held non-hot
settings at the high preset; the other held them at the low preset. These are
two rendering contexts, not a comparison between commits.

Analysis used `tools/graphics-benchmark-analyze`, which pairs identical logical
frames along the 1,024 one-feature cube edges and evaluates pairwise
interactions as differences of differences over the cube's square faces.

## Low non-hot baseline

The low baseline retains half-resolution scene rendering with terrain shadows,
placed vegetation, and motion blur disabled.

- Empty hot mask: 6.057 ms median, 6.351 ms p95.
- Ocean alone: 6.900 ms median, 8.674 ms p95.
- Grass alone: 11.004 ms median, 16.244 ms p95.
- Every hot feature: 9.419 ms median, 11.089 ms p95.
- Fastest configuration: 6.057 ms median.
- Slowest configuration: 12.825 ms median.
- No configuration exceeded the 16.67 ms budget at the median.
- Exactly 128 configurations exceeded the 8.33 ms budget at the median.

The split was exact: all 128 grass-off configurations stayed below 8.33 ms at
the median, while all 128 grass-on configurations exceeded it. Across all
contexts, grass added 2.847 ms on average and ocean added 0.860 ms. The other
features were much smaller; automatic exposure averaged +0.165 ms and the
remaining individual averages were close to the run's contextual variation.

Grass and ocean had a +0.450 ms mean interaction in the low context. Particle
cost was context-sensitive: approximately +0.190 ms without grass or ocean,
but -0.444 ms when both were enabled. The negative conditional value should
not be read as particles performing negative work. Together with the fact that
the complete hot mask was faster than grass alone, it suggests GPU frequency,
tile scheduling, or another workload-dependent interaction that a simple
additive model cannot represent.

## High non-hot baseline

The high context fixes full scene resolution, terrain shadows, placed
vegetation, and motion blur on.

- Fastest configuration: 9.041 ms median.
- Slowest configuration: 25.353 ms median.
- Every configuration exceeded the 8.33 ms budget at the median.
- 127 configurations exceeded the 16.67 ms budget at the median.
- Grass averaged +7.884 ms across cube edges.
- Ocean averaged +2.894 ms.

The high context therefore cannot isolate the 120 Hz cliff: its fixed non-hot
work already exceeds the budget. It does show that feature cost is strongly
contextual. Grass x ocean averaged a -3.632 ms interaction, and particles
shifted from positive cost in the lightest contexts to a large negative
conditional coefficient in grass-heavy contexts. Replicated and counter-
ordered runs are needed before attributing those effects specifically to GPU
dynamic frequency or tile scheduling.

## Immediate reading

For the current deterministic riding segment and machine, procedural grass is
the feature that separates the low preset's reliable 120 Hz region from the
60 Hz presentation cliff. Ocean adds meaningful cost and raises tail latency,
but it does not cross the median deadline without grass. Small post and effect
features are secondary at this scale.

The cube also falsifies a purely additive cost model. Node, edge, and square-
face measurements should be retained together: a feature's isolated cost is
not sufficient to predict its cost inside a heavy rendering configuration.

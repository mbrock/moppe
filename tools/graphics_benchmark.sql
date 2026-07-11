CREATE OR REPLACE TABLE samples AS
SELECT * FROM read_csv_auto(getvariable('input'));

CREATE OR REPLACE TABLE features(bit, name) AS VALUES
  (0, 'grass'),
  (1, 'ocean'),
  (2, 'particles'),
  (3, 'vehicle-effects'),
  (4, 'star-effects'),
  (5, 'bloom'),
  (6, 'auto-exposure'),
  (7, 'lens-flare');

CREATE OR REPLACE TABLE configuration_stats AS
SELECT mask,
       count(*) AS frames,
       avg(gpu_ms) AS mean_gpu_ms,
       median(gpu_ms) AS median_gpu_ms,
       quantile_cont(gpu_ms, 0.05) AS p05_gpu_ms,
       quantile_cont(gpu_ms, 0.95) AS p95_gpu_ms,
       stddev_samp(gpu_ms) AS stddev_gpu_ms,
       min(gpu_ms) AS min_gpu_ms,
       max(gpu_ms) AS max_gpu_ms,
       avg((gpu_ms > 1000.0 / 120.0)::INT) AS miss_120hz_rate,
       avg((gpu_ms > 1000.0 / 60.0)::INT) AS miss_60hz_rate
FROM samples
GROUP BY mask;

-- Every directed hypercube edge starts at a mask where the selected bit is
-- zero and ends at the otherwise identical mask where it is one. Pair on the
-- logical frame so scene variation cancels before aggregation.
CREATE OR REPLACE TABLE edge_samples AS
SELECT f.bit,
       f.name AS feature,
       off.mask AS context_mask,
       off.logical_frame,
       off.gpu_ms AS off_gpu_ms,
       on_sample.gpu_ms AS on_gpu_ms,
       on_sample.gpu_ms - off.gpu_ms AS delta_gpu_ms
FROM features f
JOIN samples off ON (off.mask & (1 << f.bit)) = 0
JOIN samples on_sample
  ON on_sample.mask = (off.mask | (1 << f.bit))
 AND on_sample.logical_frame = off.logical_frame;

CREATE OR REPLACE TABLE feature_effects AS
SELECT bit,
       feature,
       count(*) AS paired_frames,
       avg(delta_gpu_ms) AS mean_delta_gpu_ms,
       median(delta_gpu_ms) AS median_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.05) AS p05_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.95) AS p95_delta_gpu_ms,
       stddev_samp(delta_gpu_ms) AS stddev_delta_gpu_ms,
       avg((delta_gpu_ms > 0)::INT) AS slower_fraction
FROM edge_samples
GROUP BY bit, feature
ORDER BY mean_delta_gpu_ms DESC;

CREATE OR REPLACE TABLE cube_edges AS
SELECT context_mask AS source_mask,
       context_mask | (1 << bit) AS target_mask,
       bit,
       feature,
       avg(delta_gpu_ms) AS mean_delta_gpu_ms,
       median(delta_gpu_ms) AS median_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.95) AS p95_delta_gpu_ms,
       stddev_samp(delta_gpu_ms) AS stddev_delta_gpu_ms
FROM edge_samples
GROUP BY context_mask, bit, feature
ORDER BY source_mask, bit;

CREATE OR REPLACE TABLE feature_correlations AS
SELECT f.bit,
       f.name AS feature,
       corr(s.gpu_ms, ((s.mask & (1 << f.bit)) != 0)::INT)
         AS gpu_correlation
FROM features f
CROSS JOIN samples s
GROUP BY f.bit, f.name
ORDER BY abs(gpu_correlation) DESC;

-- Difference of differences over every square face of the Boolean cube.
CREATE OR REPLACE TABLE pairwise_interaction_samples AS
SELECT a.bit AS bit_a,
       a.name AS feature_a,
       b.bit AS bit_b,
       b.name AS feature_b,
       s00.mask AS context_mask,
       s00.logical_frame,
       s11.gpu_ms - s10.gpu_ms - s01.gpu_ms + s00.gpu_ms
         AS interaction_gpu_ms
FROM features a
JOIN features b ON a.bit < b.bit
JOIN samples s00
  ON (s00.mask & (1 << a.bit)) = 0
 AND (s00.mask & (1 << b.bit)) = 0
JOIN samples s10
  ON s10.mask = (s00.mask | (1 << a.bit))
 AND s10.logical_frame = s00.logical_frame
JOIN samples s01
  ON s01.mask = (s00.mask | (1 << b.bit))
 AND s01.logical_frame = s00.logical_frame
JOIN samples s11
  ON s11.mask = (s00.mask | (1 << a.bit) | (1 << b.bit))
 AND s11.logical_frame = s00.logical_frame;

CREATE OR REPLACE TABLE pairwise_interactions AS
SELECT bit_a,
       feature_a,
       bit_b,
       feature_b,
       count(*) AS cube_faces_x_frames,
       avg(interaction_gpu_ms) AS mean_interaction_gpu_ms,
       median(interaction_gpu_ms) AS median_interaction_gpu_ms,
       quantile_cont(interaction_gpu_ms, 0.05) AS p05_interaction_gpu_ms,
       quantile_cont(interaction_gpu_ms, 0.95) AS p95_interaction_gpu_ms,
       stddev_samp(interaction_gpu_ms) AS stddev_interaction_gpu_ms
FROM pairwise_interaction_samples
GROUP BY bit_a, feature_a, bit_b, feature_b
ORDER BY abs(mean_interaction_gpu_ms) DESC;

CREATE OR REPLACE TABLE logical_frame_stats AS
SELECT logical_frame,
       avg(gpu_ms) AS mean_gpu_ms,
       median(gpu_ms) AS median_gpu_ms,
       quantile_cont(gpu_ms, 0.95) AS p95_gpu_ms,
       stddev_samp(gpu_ms) AS stddev_across_configurations_ms,
       min(gpu_ms) AS fastest_configuration_ms,
       max(gpu_ms) AS slowest_configuration_ms
FROM samples
GROUP BY logical_frame
ORDER BY logical_frame;

CREATE OR REPLACE VIEW configurations AS
SELECT c.*,
       (c.mask & 1) != 0 AS grass,
       (c.mask & 2) != 0 AS ocean,
       (c.mask & 4) != 0 AS particles,
       (c.mask & 8) != 0 AS vehicle_effects,
       (c.mask & 16) != 0 AS star_effects,
       (c.mask & 32) != 0 AS bloom,
       (c.mask & 64) != 0 AS auto_exposure,
       (c.mask & 128) != 0 AS lens_flare
FROM configuration_stats c;

CREATE OR REPLACE TABLE deadline_summary AS
SELECT count(*) AS configurations,
       count(*) FILTER (WHERE median_gpu_ms > 1000.0 / 120.0)
         AS median_misses_120hz,
       count(*) FILTER (WHERE p95_gpu_ms > 1000.0 / 120.0)
         AS p95_misses_120hz,
       count(*) FILTER (WHERE median_gpu_ms > 1000.0 / 60.0)
         AS median_misses_60hz,
       min(median_gpu_ms) AS fastest_median_gpu_ms,
       max(median_gpu_ms) AS slowest_median_gpu_ms
FROM configuration_stats;

COPY configuration_stats TO 'configuration-stats.csv' (HEADER);
COPY feature_effects TO 'feature-effects.csv' (HEADER);
COPY cube_edges TO 'cube-edges.csv' (HEADER);
COPY feature_correlations TO 'feature-correlations.csv' (HEADER);
COPY pairwise_interactions TO 'pairwise-interactions.csv' (HEADER);
COPY logical_frame_stats TO 'logical-frame-stats.csv' (HEADER);
COPY deadline_summary TO 'deadline-summary.csv' (HEADER);

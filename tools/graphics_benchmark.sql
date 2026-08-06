CREATE OR REPLACE TABLE samples AS
SELECT * FROM read_csv_auto(getvariable('input'));

-- The application is the schema authority. Encoded column names make a CSV
-- independently analyzable while keeping ordinary numeric rows rectangular.
CREATE OR REPLACE TABLE feature_samples AS
SELECT epoch, mask, partition_mask, logical_frame, gpu_ms,
       regexp_extract(encoded, '^feature_([0-9]+)_', 1)::INTEGER AS bit,
       regexp_extract(encoded, '^feature_[0-9]+_(.*)$', 1) AS name,
       enabled::BOOLEAN AS enabled
FROM (UNPIVOT samples ON COLUMNS('^feature_[0-9]+_')
      INTO NAME encoded VALUE enabled);

CREATE OR REPLACE TABLE features AS
SELECT DISTINCT bit, name FROM feature_samples ORDER BY bit;

CREATE OR REPLACE TABLE block_samples AS
SELECT epoch, mask, partition_mask, logical_frame, gpu_ms,
       regexp_extract(encoded, '^block_([0-9]+)_', 1)::INTEGER AS bit,
       regexp_extract(encoded, '^block_[0-9]+_(.*)$', 1) AS name,
       enabled::BOOLEAN AS enabled
FROM (UNPIVOT samples ON COLUMNS('^block_[0-9]+_')
      INTO NAME encoded VALUE enabled);

CREATE OR REPLACE TABLE partition_blocks AS
SELECT DISTINCT bit, name FROM block_samples ORDER BY bit;

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

-- Every directed quotient-cube edge starts at a partition mask where the
-- selected block is zero and ends at the otherwise identical mask where it is
-- one. Pair on logical frame so scene variation cancels before aggregation.
CREATE OR REPLACE TABLE partition_edge_samples AS
SELECT f.bit,
       f.name AS block,
       off.partition_mask AS context_mask,
       off.logical_frame,
       off.gpu_ms AS off_gpu_ms,
       on_sample.gpu_ms AS on_gpu_ms,
       on_sample.gpu_ms - off.gpu_ms AS delta_gpu_ms
FROM partition_blocks f
JOIN samples off ON (off.partition_mask & (1 << f.bit)) = 0
JOIN samples on_sample
  ON on_sample.partition_mask = (off.partition_mask | (1 << f.bit))
 AND on_sample.logical_frame = off.logical_frame;

CREATE OR REPLACE TABLE block_effects AS
SELECT bit,
       block,
       count(*) AS paired_frames,
       avg(delta_gpu_ms) AS mean_delta_gpu_ms,
       median(delta_gpu_ms) AS median_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.05) AS p05_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.95) AS p95_delta_gpu_ms,
       stddev_samp(delta_gpu_ms) AS stddev_delta_gpu_ms,
       avg((delta_gpu_ms > 0)::INT) AS slower_fraction
FROM partition_edge_samples
GROUP BY bit, block
ORDER BY mean_delta_gpu_ms DESC;

CREATE OR REPLACE TABLE partition_edges AS
SELECT context_mask AS source_mask,
       context_mask | (1 << bit) AS target_mask,
       bit,
       block,
       avg(delta_gpu_ms) AS mean_delta_gpu_ms,
       median(delta_gpu_ms) AS median_delta_gpu_ms,
       quantile_cont(delta_gpu_ms, 0.95) AS p95_delta_gpu_ms,
       stddev_samp(delta_gpu_ms) AS stddev_delta_gpu_ms
FROM partition_edge_samples
GROUP BY context_mask, bit, block
ORDER BY source_mask, bit;

CREATE OR REPLACE TABLE feature_correlations AS
SELECT f.bit,
       f.name AS feature,
       corr(s.gpu_ms, s.enabled::INT) AS gpu_correlation
FROM features f
JOIN feature_samples s USING (bit, name)
GROUP BY f.bit, f.name
ORDER BY abs(gpu_correlation) DESC;

-- Difference of differences over every square face of the Boolean cube.
CREATE OR REPLACE TABLE pairwise_interaction_samples AS
SELECT a.bit AS bit_a,
       a.name AS block_a,
       b.bit AS bit_b,
       b.name AS block_b,
       s00.partition_mask AS context_mask,
       s00.logical_frame,
       s11.gpu_ms - s10.gpu_ms - s01.gpu_ms + s00.gpu_ms
         AS interaction_gpu_ms
FROM partition_blocks a
JOIN partition_blocks b ON a.bit < b.bit
JOIN samples s00
  ON (s00.partition_mask & (1 << a.bit)) = 0
 AND (s00.partition_mask & (1 << b.bit)) = 0
JOIN samples s10
  ON s10.partition_mask = (s00.partition_mask | (1 << a.bit))
 AND s10.logical_frame = s00.logical_frame
JOIN samples s01
  ON s01.partition_mask = (s00.partition_mask | (1 << b.bit))
 AND s01.logical_frame = s00.logical_frame
JOIN samples s11
  ON s11.partition_mask =
       (s00.partition_mask | (1 << a.bit) | (1 << b.bit))
 AND s11.logical_frame = s00.logical_frame;

CREATE OR REPLACE TABLE pairwise_interactions AS
SELECT bit_a,
       block_a,
       bit_b,
       block_b,
       count(*) AS cube_faces_x_frames,
       avg(interaction_gpu_ms) AS mean_interaction_gpu_ms,
       median(interaction_gpu_ms) AS median_interaction_gpu_ms,
       quantile_cont(interaction_gpu_ms, 0.05) AS p05_interaction_gpu_ms,
       quantile_cont(interaction_gpu_ms, 0.95) AS p95_interaction_gpu_ms,
       stddev_samp(interaction_gpu_ms) AS stddev_interaction_gpu_ms
FROM pairwise_interaction_samples
GROUP BY bit_a, block_a, bit_b, block_b
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
COPY block_effects TO 'block-effects.csv' (HEADER);
COPY partition_edges TO 'partition-edges.csv' (HEADER);
COPY feature_correlations TO 'feature-correlations.csv' (HEADER);
COPY pairwise_interactions TO 'pairwise-interactions.csv' (HEADER);
COPY logical_frame_stats TO 'logical-frame-stats.csv' (HEADER);
COPY deadline_summary TO 'deadline-summary.csv' (HEADER);
COPY features TO 'features.csv' (HEADER);
COPY partition_blocks TO 'partition-blocks.csv' (HEADER);

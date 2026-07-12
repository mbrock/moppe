"""Archive a Tracy capture and import its CPU zones into DuckDB/Parquet."""

import argparse
import csv
import datetime
import hashlib
import io
import json
import pathlib
import shutil
import subprocess

import duckdb


ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_DATABASE = ROOT / "build-tracy/traces.duckdb"
DEFAULT_ARCHIVE = ROOT / "build-tracy/archive"


def git_revision():
  return subprocess.run(
      ["git", "-C", str(ROOT), "rev-parse", "HEAD"], check=True,
      text=True, stdout=subprocess.PIPE).stdout.strip()


def git_dirty():
  result = subprocess.run(
      ["git", "-C", str(ROOT), "status", "--porcelain"], check=True,
      text=True, stdout=subprocess.PIPE)
  return bool(result.stdout)


def capture_id(path):
  digest = hashlib.sha256()
  with path.open("rb") as source:
    while block := source.read(1024 * 1024):
      digest.update(block)
  return digest.hexdigest()


def sql_string(value):
  return str(value).replace("'", "''")


def exported_zones(path):
  result = subprocess.run(
      ["tracy-csvexport", "-u", str(path)], check=True, text=True,
      stdout=subprocess.PIPE)
  rows = []
  for index, row in enumerate(csv.DictReader(io.StringIO(result.stdout)), 1):
    rows.append((
        index, row["name"], row["src_file"], int(row["src_line"]),
        int(row["ns_since_start"]), int(row["exec_time_ns"]),
        int(row["thread"]), row["value"] or None))
  return rows


def exported_gpu_zones(path):
  result = subprocess.run(
      ["tracy-csvexport", "-u", "-g", str(path)], check=True, text=True,
      stdout=subprocess.PIPE)
  rows = []
  for index, row in enumerate(csv.DictReader(io.StringIO(result.stdout)), 1):
    rows.append((
        index, row["name"], row["src_file"],
        int(row["Time from start of program"]),
        int(row["GPU execution time"])))
  return rows


def exported_plots(path):
  result = subprocess.run(
      ["tracy-csvexport", "-u", "-p", str(path)], check=True, text=True,
      stdout=subprocess.PIPE)
  rows = []
  indices = {}
  for row in csv.DictReader(io.StringIO(result.stdout)):
    if row["src_file"] or not row["name"].startswith("benchmark."):
      continue
    index = indices.get(row["name"], 0) + 1
    indices[row["name"]] = index
    rows.append((
        row["name"], index, int(row["ns_since_start"]),
        float(row["value"])))
  return rows


def initialize(connection):
  connection.execute("""
    CREATE TABLE IF NOT EXISTS captures (
      capture_id VARCHAR PRIMARY KEY,
      git_commit VARCHAR,
      git_dirty BOOLEAN,
      executable_sha256 VARCHAR,
      trace_path VARCHAR,
      trace_bytes UBIGINT,
      zone_count UBIGINT,
      imported_at TIMESTAMPTZ
    )
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS zones (
      capture_id VARCHAR,
      zone_instance UBIGINT,
      name VARCHAR,
      source_file VARCHAR,
      source_line INTEGER,
      start_ns UBIGINT,
      duration_ns UBIGINT,
      thread_id UBIGINT,
      value VARCHAR,
      PRIMARY KEY (capture_id, zone_instance)
    )
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS gpu_zones (
      capture_id VARCHAR,
      zone_instance UBIGINT,
      name VARCHAR,
      source_file VARCHAR,
      start_ns UBIGINT,
      duration_ns UBIGINT,
      PRIMARY KEY (capture_id, zone_instance)
    )
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS plots (
      capture_id VARCHAR,
      name VARCHAR,
      point_index UBIGINT,
      timestamp_ns UBIGINT,
      value DOUBLE,
      PRIMARY KEY (capture_id, name, point_index)
    )
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_frame_coordinates AS
    SELECT capture_id, point_index,
           min(timestamp_ns) AS timestamp_ns,
           max(value) FILTER (name='benchmark.mask')::UINTEGER AS mask,
           max(value) FILTER (name='benchmark.epoch')::UINTEGER AS epoch,
           max(value) FILTER (name='benchmark.logical_frame')::UINTEGER
             AS logical_frame,
           max(value) FILTER (name='benchmark.measured')::BOOLEAN AS measured
    FROM plots
    GROUP BY capture_id, point_index
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_gpu_zones AS
    SELECT g.*, f.point_index AS frame_index, f.mask, f.epoch,
           f.logical_frame, f.measured
    FROM gpu_zones g ASOF JOIN benchmark_frame_coordinates f
      ON g.capture_id=f.capture_id AND g.start_ns >= f.timestamp_ns
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_cpu_zones AS
    SELECT z.*, f.point_index AS frame_index, f.mask, f.epoch,
           f.logical_frame, f.measured
    FROM zones z ASOF JOIN benchmark_frame_coordinates f
      ON z.capture_id=f.capture_id
     AND z.start_ns + z.duration_ns >= f.timestamp_ns
  """)
  connection.execute("""
    CREATE OR REPLACE TABLE benchmark_features(bit, name) AS VALUES
      (0, 'grass'), (1, 'ocean'), (2, 'particles'),
      (3, 'vehicle-effects'), (4, 'star-effects'), (5, 'bloom'),
      (6, 'auto-exposure'), (7, 'lens-flare')
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_gpu_frame_zones AS
    SELECT capture_id, frame_index, mask, epoch, logical_frame, name,
           sum(duration_ns) AS duration_ns, count(*) AS instances
    FROM benchmark_gpu_zones WHERE measured
    GROUP BY capture_id, frame_index, mask, epoch, logical_frame, name
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_cpu_frame_zones AS
    SELECT capture_id, frame_index, mask, epoch, logical_frame, name,
           sum(duration_ns) AS duration_ns, count(*) AS instances
    FROM benchmark_cpu_zones WHERE measured
    GROUP BY capture_id, frame_index, mask, epoch, logical_frame, name
  """)
  connection.execute("""
    CREATE OR REPLACE VIEW benchmark_zone_feature_effects AS
    WITH raw_zones AS (
      SELECT 'gpu' AS domain, * FROM benchmark_gpu_frame_zones
      UNION ALL BY NAME
      SELECT 'cpu' AS domain, * FROM benchmark_cpu_frame_zones
    ), frames AS (
      SELECT * FROM benchmark_frame_coordinates WHERE measured
    ), names AS (
      SELECT DISTINCT capture_id, domain, name FROM raw_zones
    ), all_zones AS (
      SELECT f.capture_id, n.domain, n.name, f.point_index AS frame_index,
             f.mask, f.epoch, f.logical_frame,
             coalesce(z.duration_ns, 0) AS duration_ns,
             coalesce(z.instances, 0) AS instances
      FROM frames f JOIN names n USING (capture_id)
      LEFT JOIN raw_zones z
        ON z.capture_id=f.capture_id AND z.frame_index=f.point_index
       AND z.domain=n.domain AND z.name=n.name
    ), edges AS (
      SELECT off.capture_id, off.domain, off.name, f.bit,
             f.name AS feature,
             (on_zone.duration_ns-off.duration_ns) / 1000000.0 AS delta_ms
      FROM benchmark_features f
      JOIN all_zones off ON (off.mask & (1 << f.bit))=0
      JOIN all_zones on_zone
        ON on_zone.capture_id=off.capture_id
       AND on_zone.domain=off.domain AND on_zone.name=off.name
       AND on_zone.mask=(off.mask | (1 << f.bit))
       AND on_zone.logical_frame=off.logical_frame
    )
    SELECT capture_id, domain, name, bit, feature,
           count(*) AS paired_frames,
           avg(delta_ms) AS mean_delta_ms,
           median(delta_ms) AS median_delta_ms,
           quantile_cont(delta_ms, 0.95) AS p95_delta_ms
    FROM edges
    GROUP BY capture_id, domain, name, bit, feature
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS benchmark_runs (
      capture_id VARCHAR PRIMARY KEY,
      csv_sha256 VARCHAR,
      csv_path VARCHAR,
      sample_count UBIGINT
    )
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS benchmark_samples (
      capture_id VARCHAR,
      epoch UINTEGER,
      mask UINTEGER,
      logical_frame UINTEGER,
      gpu_ms DOUBLE,
      features_json JSON,
      PRIMARY KEY (capture_id, epoch, logical_frame)
    )
  """)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("trace", type=pathlib.Path)
  parser.add_argument("--database", type=pathlib.Path,
                      default=DEFAULT_DATABASE)
  parser.add_argument("--archive", type=pathlib.Path, default=DEFAULT_ARCHIVE)
  parser.add_argument("--executable", type=pathlib.Path,
                      help="profiled executable, used to record a build hash")
  parser.add_argument("--benchmark-csv", type=pathlib.Path,
                      help="graphics feature-cube samples captured with trace")
  args = parser.parse_args()
  trace = args.trace.expanduser().resolve()
  if not trace.is_file():
    raise SystemExit(f"trace does not exist: {trace}")
  executable = (args.executable.expanduser().resolve()
                if args.executable else None)
  if executable and not executable.is_file():
    raise SystemExit(f"executable does not exist: {executable}")
  benchmark_csv = (args.benchmark_csv.expanduser().resolve()
                   if args.benchmark_csv else None)
  if benchmark_csv and not benchmark_csv.is_file():
    raise SystemExit(f"benchmark CSV does not exist: {benchmark_csv}")

  identity = capture_id(trace)
  destination = args.archive / identity
  destination.mkdir(parents=True, exist_ok=True)
  archived_trace = destination / "capture.tracy"
  if not archived_trace.exists():
    shutil.copy2(trace, archived_trace)

  zones = exported_zones(archived_trace)
  gpu_zones = exported_gpu_zones(archived_trace)
  plots = exported_plots(archived_trace)
  args.database.parent.mkdir(parents=True, exist_ok=True)
  connection = duckdb.connect(str(args.database))
  initialize(connection)
  connection.execute("DELETE FROM zones WHERE capture_id=?", [identity])
  connection.execute("DELETE FROM gpu_zones WHERE capture_id=?", [identity])
  connection.execute("DELETE FROM plots WHERE capture_id=?", [identity])
  connection.execute("DELETE FROM captures WHERE capture_id=?", [identity])
  connection.execute(
      "INSERT INTO captures VALUES (?, ?, ?, ?, ?, ?, ?, ?)", [
          identity, git_revision(), git_dirty(),
          capture_id(executable) if executable else None, str(archived_trace),
          archived_trace.stat().st_size, len(zones),
          datetime.datetime.now(datetime.timezone.utc),
      ])
  connection.executemany(
      "INSERT INTO zones VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
      [(identity, *row) for row in zones])
  if gpu_zones:
    connection.executemany(
        "INSERT INTO gpu_zones VALUES (?, ?, ?, ?, ?, ?)",
        [(identity, *row) for row in gpu_zones])
  if plots:
    connection.executemany(
        "INSERT INTO plots VALUES (?, ?, ?, ?, ?)",
        [(identity, *row) for row in plots])
  benchmark_rows = []
  if benchmark_csv:
    archived_csv = destination / "benchmark.csv"
    shutil.copy2(benchmark_csv, archived_csv)
    with archived_csv.open(newline="") as stream:
      for row in csv.DictReader(stream):
        feature_values = {
            name: value for name, value in row.items()
            if name not in {"epoch", "mask", "logical_frame", "gpu_ms"}
        }
        benchmark_rows.append((
            identity, int(row["epoch"]), int(row["mask"]),
            int(row["logical_frame"]), float(row["gpu_ms"]),
            json.dumps(feature_values, sort_keys=True)))
    connection.execute(
        "DELETE FROM benchmark_samples WHERE capture_id=?", [identity])
    connection.execute(
        "DELETE FROM benchmark_runs WHERE capture_id=?", [identity])
    connection.execute("INSERT INTO benchmark_runs VALUES (?, ?, ?, ?)", [
        identity, capture_id(archived_csv), str(archived_csv),
        len(benchmark_rows)])
    connection.executemany(
        "INSERT INTO benchmark_samples VALUES (?, ?, ?, ?, ?, ?)",
        benchmark_rows)
  parquet = destination / "zones.parquet"
  connection.execute(
      f"COPY (SELECT * FROM zones WHERE capture_id='{identity}') "
      f"TO '{sql_string(parquet)}' (FORMAT PARQUET, COMPRESSION ZSTD)")
  gpu_parquet = destination / "gpu-zones.parquet"
  connection.execute(
      f"COPY (SELECT * FROM gpu_zones WHERE capture_id='{identity}') "
      f"TO '{sql_string(gpu_parquet)}' (FORMAT PARQUET, COMPRESSION ZSTD)")
  plots_parquet = destination / "plots.parquet"
  connection.execute(
      f"COPY (SELECT * FROM plots WHERE capture_id='{identity}') "
      f"TO '{sql_string(plots_parquet)}' (FORMAT PARQUET, COMPRESSION ZSTD)")
  if benchmark_rows:
    benchmark_parquet = destination / "benchmark-samples.parquet"
    connection.execute(
        f"COPY (SELECT * FROM benchmark_samples "
        f"WHERE capture_id='{identity}') TO '{sql_string(benchmark_parquet)}' "
        "(FORMAT PARQUET, COMPRESSION ZSTD)")
  connection.close()

  print(f"Capture: {identity}")
  print(f"Archive: {archived_trace}")
  print(f"Zones:   {len(zones)}")
  print(f"GPU:     {len(gpu_zones)} zones")
  print(f"Plots:   {len(plots)} points")
  print(f"DuckDB:  {args.database}")
  print(f"Parquet: {parquet}")
  if benchmark_rows:
    print(f"Samples: {len(benchmark_rows)}")


if __name__ == "__main__":
  main()

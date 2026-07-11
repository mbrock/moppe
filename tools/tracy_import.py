"""Archive a Tracy capture and import its CPU zones into DuckDB/Parquet."""

import argparse
import csv
import datetime
import hashlib
import io
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


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("trace", type=pathlib.Path)
  parser.add_argument("--database", type=pathlib.Path,
                      default=DEFAULT_DATABASE)
  parser.add_argument("--archive", type=pathlib.Path, default=DEFAULT_ARCHIVE)
  parser.add_argument("--executable", type=pathlib.Path,
                      help="profiled executable, used to record a build hash")
  args = parser.parse_args()
  trace = args.trace.expanduser().resolve()
  if not trace.is_file():
    raise SystemExit(f"trace does not exist: {trace}")
  executable = (args.executable.expanduser().resolve()
                if args.executable else None)
  if executable and not executable.is_file():
    raise SystemExit(f"executable does not exist: {executable}")

  identity = capture_id(trace)
  destination = args.archive / identity
  destination.mkdir(parents=True, exist_ok=True)
  archived_trace = destination / "capture.tracy"
  if not archived_trace.exists():
    shutil.copy2(trace, archived_trace)

  zones = exported_zones(archived_trace)
  args.database.parent.mkdir(parents=True, exist_ok=True)
  connection = duckdb.connect(str(args.database))
  initialize(connection)
  connection.execute("DELETE FROM zones WHERE capture_id=?", [identity])
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
  parquet = destination / "zones.parquet"
  connection.execute(
      f"COPY (SELECT * FROM zones WHERE capture_id='{identity}') "
      f"TO '{sql_string(parquet)}' (FORMAT PARQUET, COMPRESSION ZSTD)")
  connection.close()

  print(f"Capture: {identity}")
  print(f"Archive: {archived_trace}")
  print(f"Zones:   {len(zones)}")
  print(f"DuckDB:  {args.database}")
  print(f"Parquet: {parquet}")


if __name__ == "__main__":
  main()

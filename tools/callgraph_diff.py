"""Compare complexity and call-graph metrics between two Git commits."""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

import duckdb


ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL_FILES = [
    ".gitignore",
    "CMakePresets.json",
    "cmake/toolchains/homebrew-llvm.cmake",
    "tools/callgraph-analyze",
    "tools/callgraph-report",
    "tools/callgraph_analyze.py",
    "tools/callgraph_report.py",
    "tools/complexity-report",
]
ANALYSIS_TOOL_FILES = [path for path in TOOL_FILES if path != ".gitignore"]
CACHE_VERSION = 1
CACHE_TABLES = [
    "nodes", "edges", "edge_ids", "pagerank", "betweenness",
    "components", "communities", "entrypoint_exposure",
]
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".m", ".mm",
}


def run(command, **kwargs):
  print("+ " + " ".join(map(str, command)), file=sys.stderr)
  return subprocess.run(command, check=True, **kwargs)


def git(*arguments):
  result = subprocess.run(
      ["git", "-C", str(ROOT), *arguments], check=True,
      text=True, stdout=subprocess.PIPE)
  return result.stdout.strip()


def command_output(command):
  return subprocess.run(
      command, check=True, text=True, stdout=subprocess.PIPE
  ).stdout.strip()


def relevant_source_objects(revision):
  rows = []
  listing = git("ls-tree", "-r", "--full-tree", revision)
  for line in listing.splitlines():
    metadata, path = line.split("\t", 1)
    suffix = pathlib.PurePosixPath(path).suffix.lower()
    if (suffix in SOURCE_SUFFIXES or path == "CMakeLists.txt" or
        path.startswith("cmake/") or path.startswith("CMakePresets")):
      _, kind, object_id = metadata.split()
      if kind == "blob":
        rows.append((path, object_id))
  return rows


def file_digest(paths):
  digest = hashlib.sha256()
  for path in paths:
    digest.update(str(path.relative_to(ROOT)).encode())
    digest.update(b"\0")
    digest.update(path.read_bytes())
    digest.update(b"\0")
  return digest.hexdigest()


def cache_manifest(revision):
  source_objects = relevant_source_objects(revision)
  source_digest = hashlib.sha256(
      json.dumps(source_objects, separators=(",", ":")).encode()
  ).hexdigest()
  tool_paths = [ROOT / path for path in ANALYSIS_TOOL_FILES
                if (ROOT / path).is_file()]
  try:
    llvm = command_output(["brew", "--prefix", "llvm@20"])
  except subprocess.CalledProcessError:
    llvm = command_output(["brew", "--prefix", "llvm"])
  clang_version = command_output([str(pathlib.Path(llvm) / "bin/clang"),
                                  "--version"]).splitlines()[0]
  duckdb_version = duckdb.__version__
  identity = {
      "cache_version": CACHE_VERSION,
      "source_digest": source_digest,
      "tool_digest": file_digest(tool_paths),
      "clang_version": clang_version,
      "duckdb_version": duckdb_version,
      "system": platform.system(),
      "architecture": platform.machine(),
  }
  key = hashlib.sha256(
      json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
  ).hexdigest()
  return {
      **identity,
      "cache_key": key,
      "revision": revision,
      "source_files": len(source_objects),
  }


def default_cache_root():
  override = os.environ.get("MOPPE_ANALYSIS_CACHE")
  if override:
    return pathlib.Path(override).expanduser().resolve()
  common = pathlib.Path(git("rev-parse", "--git-common-dir"))
  if not common.is_absolute():
    common = ROOT / common
  return common.resolve() / "moppe-analysis-cache" / f"v{CACHE_VERSION}"


def snapshot_dir(cache_root, manifest):
  return cache_root / "snapshots" / manifest["cache_key"]


def export_parquet(connection, table, path, cache_key):
  key = cache_key.replace("'", "''")
  connection.execute(
      f"COPY (SELECT '{key}' AS cache_key, * FROM {table}) "
      f"TO '{sql_path(path)}' (FORMAT PARQUET, COMPRESSION ZSTD)")


def store_snapshot(cache_root, source_db, manifest):
  destination = snapshot_dir(cache_root, manifest)
  if (destination / "snapshot.duckdb").is_file():
    return destination / "snapshot.duckdb"
  destination.parent.mkdir(parents=True, exist_ok=True)
  staging = pathlib.Path(tempfile.mkdtemp(
      prefix=destination.name + ".tmp-", dir=destination.parent))
  cached_db = staging / "snapshot.duckdb"
  shutil.copy2(source_db, cached_db)
  connection = duckdb.connect(str(cached_db))
  connection.execute("CREATE TABLE analysis_snapshot AS SELECT ? AS cache_key, "
                     "? AS revision, ? AS source_digest, ? AS tool_digest, "
                     "? AS clang_version, ? AS duckdb_version, "
                     "? AS system, ? AS architecture, ? AS source_files, "
                     "?::TIMESTAMPTZ AS created_at", [
                         manifest["cache_key"], manifest["revision"],
                         manifest["source_digest"], manifest["tool_digest"],
                         manifest["clang_version"], manifest["duckdb_version"],
                         manifest["system"], manifest["architecture"],
                         manifest["source_files"],
                         datetime.datetime.now(datetime.timezone.utc).isoformat(),
                     ])
  for table in CACHE_TABLES:
    export_parquet(connection, table, staging / f"{table}.parquet",
                   manifest["cache_key"])
  connection.close()
  (staging / "manifest.json").write_text(
      json.dumps(manifest, indent=2, sort_keys=True) + "\n")
  try:
    staging.rename(destination)
  except FileExistsError:
    shutil.rmtree(staging)
  return destination / "snapshot.duckdb"


def refresh_catalog(cache_root, observed_manifests=()):
  cache_root.mkdir(parents=True, exist_ok=True)
  catalog_path = cache_root / "catalog.duckdb"
  connection = duckdb.connect(str(catalog_path))
  connection.execute("DROP TABLE IF EXISTS snapshots")
  connection.execute("""
    CREATE TABLE snapshots (
      cache_key VARCHAR PRIMARY KEY, revision VARCHAR, source_digest VARCHAR,
      tool_digest VARCHAR, clang_version VARCHAR, duckdb_version VARCHAR,
      system VARCHAR, architecture VARCHAR, source_files BIGINT,
      snapshot_path VARCHAR
    )
  """)
  connection.execute("""
    CREATE TABLE IF NOT EXISTS revisions (
      revision VARCHAR, cache_key VARCHAR, observed_at TIMESTAMPTZ,
      PRIMARY KEY (revision, cache_key)
    )
  """)
  rows = []
  for path in sorted((cache_root / "snapshots").glob("*/manifest.json")):
    manifest = json.loads(path.read_text())
    rows.append(tuple(manifest[name] for name in (
        "cache_key", "revision", "source_digest", "tool_digest",
        "clang_version", "duckdb_version", "system", "architecture",
        "source_files")) + (str(path.parent / "snapshot.duckdb"),))
  if rows:
    connection.executemany("INSERT INTO snapshots VALUES (?, ?, ?, ?, ?, ?, "
                           "?, ?, ?, ?)", rows)
    parquet_root = sql_path(cache_root / "snapshots" / "*" / "{}.parquet")
    for table in CACHE_TABLES:
      connection.execute(
          f"CREATE OR REPLACE VIEW {table} AS SELECT * FROM "
          f"read_parquet('{parquet_root.format(table)}', union_by_name=true)")
  connection.execute(
      "DELETE FROM revisions WHERE cache_key NOT IN "
      "(SELECT cache_key FROM snapshots)")
  now = datetime.datetime.now(datetime.timezone.utc)
  for manifest in observed_manifests:
    connection.execute(
        "INSERT OR REPLACE INTO revisions VALUES (?, ?, ?)",
        [manifest["revision"], manifest["cache_key"], now])
  connection.close()
  return catalog_path


def sql_path(path):
  return str(path).replace("'", "''")


def export_csv(connection, table, path):
  connection.execute(
      f"COPY {table} TO '{sql_path(path)}' (HEADER, DELIMITER ',')")


def overlay_tools(worktree):
  for relative in TOOL_FILES:
    source = ROOT / relative
    destination = worktree / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def prepare_snapshot(revision, worktree):
  run(["git", "-C", str(ROOT), "worktree", "add", "--detach",
       str(worktree), revision], stdout=subprocess.DEVNULL)
  overlay_tools(worktree)


def analyze_snapshot(worktree, log_path):
  with log_path.open("w") as log:
    run([str(worktree / "tools/callgraph-report"), "-j", "1"], cwd=worktree,
        stdout=log, stderr=subprocess.STDOUT)
    run([str(worktree / "tools/callgraph-analyze")], cwd=worktree,
        stdout=log, stderr=subprocess.STDOUT)
  return (worktree / "build-homebrew/callgraph/analysis/"
          "callgraph.duckdb")


def analyze_or_restore(revision, manifest, worktree, log_path, cache_root,
                       use_cache):
  cached_db = snapshot_dir(cache_root, manifest) / "snapshot.duckdb"
  if use_cache and cached_db.is_file():
    print(f"Cache hit {manifest['cache_key'][:12]} for "
          f"{revision[:12]}", file=sys.stderr)
    return cached_db, manifest, True
  print(f"Cache miss {manifest['cache_key'][:12]} for "
        f"{revision[:12]}", file=sys.stderr)
  prepare_snapshot(revision, worktree)
  database = analyze_snapshot(worktree, log_path)
  if use_cache:
    database = store_snapshot(cache_root, database, manifest)
  return database, manifest, False


def remove_worktree(worktree):
  if worktree.exists():
    subprocess.run(
        ["git", "-C", str(ROOT), "worktree", "remove", "--force",
         str(worktree)], check=False, stdout=subprocess.DEVNULL)


def create_comparison(connection):
  connection.execute("""
    CREATE TABLE topology AS
    SELECT 'functions' AS metric,
           (SELECT count(*) FROM base.nodes) AS base_value,
           (SELECT count(*) FROM target.nodes) AS target_value
    UNION ALL SELECT 'unique project edges',
           (SELECT count(*) FROM base.edge_ids),
           (SELECT count(*) FROM target.edge_ids)
    UNION ALL SELECT 'call sites',
           (SELECT count(*) FROM base.edges),
           (SELECT count(*) FROM target.edges)
    UNION ALL SELECT 'connected nodes',
           (SELECT count(*) FROM base.components),
           (SELECT count(*) FROM target.components)
    UNION ALL SELECT 'weak components',
           (SELECT count(DISTINCT component) FROM base.components),
           (SELECT count(DISTINCT component) FROM target.components)
  """)
  connection.execute("ALTER TABLE topology ADD COLUMN delta BIGINT")
  connection.execute("UPDATE topology SET delta = target_value-base_value")

  connection.execute("""
    CREATE TABLE complexity_delta AS
    SELECT n.id, n.name, n.module, n.file,
           o.cognitive AS base_cognitive,
           n.cognitive AS target_cognitive,
           n.cognitive-o.cognitive AS cognitive_delta,
           o.cyclomatic AS base_cyclomatic,
           n.cyclomatic AS target_cyclomatic,
           n.cyclomatic-o.cyclomatic AS cyclomatic_delta
    FROM target.nodes n JOIN base.nodes o USING (id)
    WHERE n.cognitive != o.cognitive OR n.cyclomatic != o.cyclomatic
    ORDER BY abs(cognitive_delta) DESC, abs(cyclomatic_delta) DESC
  """)

  connection.execute("""
    CREATE TABLE centrality_delta AS
    SELECT n.id, n.name, n.module, n.file,
           coalesce(op.rank, 0) AS base_pagerank,
           coalesce(np.rank, 0) AS target_pagerank,
           coalesce(np.rank, 0)-coalesce(op.rank, 0) AS pagerank_delta,
           coalesce(ob.betweenness, 0) AS base_betweenness,
           coalesce(nb.betweenness, 0) AS target_betweenness,
           coalesce(nb.betweenness, 0)-coalesce(ob.betweenness, 0)
             AS betweenness_delta,
           o.cognitive*coalesce(op.rank, 0) AS base_pagerank_complexity,
           n.cognitive*coalesce(np.rank, 0) AS target_pagerank_complexity,
           n.cognitive*coalesce(np.rank, 0)
             - o.cognitive*coalesce(op.rank, 0)
             AS pagerank_complexity_delta,
           o.cognitive*coalesce(ob.betweenness, 0)
             AS base_betweenness_complexity,
           n.cognitive*coalesce(nb.betweenness, 0)
             AS target_betweenness_complexity,
           n.cognitive*coalesce(nb.betweenness, 0)
             - o.cognitive*coalesce(ob.betweenness, 0)
             AS betweenness_complexity_delta
    FROM target.nodes n JOIN base.nodes o USING (id)
    LEFT JOIN base.pagerank op ON op.node_id=o.node_id
    LEFT JOIN target.pagerank np ON np.node_id=n.node_id
    LEFT JOIN base.betweenness ob ON ob.node_id=o.node_id
    LEFT JOIN target.betweenness nb ON nb.node_id=n.node_id
    ORDER BY abs(betweenness_complexity_delta) DESC
  """)

  connection.execute("""
    CREATE TABLE entrypoint_delta AS
    WITH old AS (
      SELECT entrypoint,
        sum(cognitive_contribution)
          FILTER (node_id != entrypoint_id) AS cognitive,
        sum(cyclomatic_contribution)
          FILTER (node_id != entrypoint_id) AS cyclomatic
      FROM base.entrypoint_exposure GROUP BY entrypoint
    ), new AS (
      SELECT entrypoint,
        sum(cognitive_contribution)
          FILTER (node_id != entrypoint_id) AS cognitive,
        sum(cyclomatic_contribution)
          FILTER (node_id != entrypoint_id) AS cyclomatic
      FROM target.entrypoint_exposure GROUP BY entrypoint
    )
    SELECT coalesce(new.entrypoint, old.entrypoint) AS entrypoint,
           old.cognitive AS base_cognitive,
           new.cognitive AS target_cognitive,
           new.cognitive-old.cognitive AS cognitive_delta,
           old.cyclomatic AS base_cyclomatic,
           new.cyclomatic AS target_cyclomatic,
           new.cyclomatic-old.cyclomatic AS cyclomatic_delta
    FROM old FULL JOIN new USING (entrypoint)
    ORDER BY abs(cognitive_delta) DESC
  """)

  connection.execute("""
    CREATE TABLE module_flow_delta AS
    WITH old AS (
      SELECT a.module AS caller_module, b.module AS callee_module,
             count(*) AS call_sites
      FROM base.edges e
      JOIN base.nodes a ON a.id=e.caller_id
      JOIN base.nodes b ON b.id=e.callee_id
      WHERE e.resolution='project' AND a.module != b.module
      GROUP BY 1, 2
    ), new AS (
      SELECT a.module AS caller_module, b.module AS callee_module,
             count(*) AS call_sites
      FROM target.edges e
      JOIN target.nodes a ON a.id=e.caller_id
      JOIN target.nodes b ON b.id=e.callee_id
      WHERE e.resolution='project' AND a.module != b.module
      GROUP BY 1, 2
    )
    SELECT coalesce(new.caller_module, old.caller_module) AS caller_module,
           coalesce(new.callee_module, old.callee_module) AS callee_module,
           coalesce(old.call_sites, 0) AS base_call_sites,
           coalesce(new.call_sites, 0) AS target_call_sites,
           coalesce(new.call_sites, 0)-coalesce(old.call_sites, 0) AS delta
    FROM old FULL JOIN new USING (caller_module, callee_module)
    WHERE coalesce(new.call_sites, 0) != coalesce(old.call_sites, 0)
    ORDER BY abs(delta) DESC
  """)

  connection.execute("""
    CREATE TABLE signature_changes AS
    WITH old AS (
      SELECT name, file, min(id) AS base_id, count(*) AS matches
      FROM base.nodes WHERE name NOT LIKE '%(lambda at%'
      GROUP BY name, file
    ), new AS (
      SELECT name, file, min(id) AS target_id, count(*) AS matches
      FROM target.nodes WHERE name NOT LIKE '%(lambda at%'
      GROUP BY name, file
    )
    SELECT new.name, new.file, old.base_id, new.target_id
    FROM old JOIN new USING (name, file)
    WHERE old.matches=1 AND new.matches=1 AND old.base_id != new.target_id
    ORDER BY new.file, new.name
  """)

  connection.execute("""
    CREATE TABLE added_functions AS
    SELECT n.* FROM target.nodes n
    ANTI JOIN base.nodes o USING (id)
    WHERE n.name NOT LIKE '%(lambda at%'
      AND NOT EXISTS (
        SELECT 1 FROM signature_changes s WHERE s.target_id=n.id)
    ORDER BY n.file, n.line, n.name
  """)
  connection.execute("""
    CREATE TABLE removed_functions AS
    SELECT o.* FROM base.nodes o
    ANTI JOIN target.nodes n USING (id)
    WHERE o.name NOT LIKE '%(lambda at%'
      AND NOT EXISTS (
        SELECT 1 FROM signature_changes s WHERE s.base_id=o.id)
    ORDER BY o.file, o.line, o.name
  """)


def markdown_table(headers, rows):
  lines = ["| " + " | ".join(headers) + " |",
           "|" + "|".join("---" for _ in headers) + "|"]
  for row in rows:
    values = [str(value).replace("|", "\\|") for value in row]
    lines.append("| " + " | ".join(values) + " |")
  return lines


def write_summary(connection, output, base, target):
  lines = [
      "# Call-graph diff",
      "",
      f"Base: `{base}`  ",
      f"Target: `{target}`",
      "",
      "Both commits were analyzed with the same tool version. Louvain "
      "communities are omitted because their partitions are nondeterministic.",
      "",
      "## Topology",
      "",
  ]
  rows = connection.execute(
      "SELECT metric, base_value, target_value, delta FROM topology"
  ).fetchall()
  lines.extend(markdown_table(
      ["Metric", "Base", "Target", "Delta"], rows))

  lines.extend(["", "## Largest local complexity changes", ""])
  rows = connection.execute("""
    SELECT name, base_cognitive, target_cognitive, cognitive_delta,
           base_cyclomatic, target_cyclomatic, cyclomatic_delta
    FROM complexity_delta LIMIT 20
  """).fetchall()
  lines.extend(markdown_table(
      ["Function", "Cog base", "Cog target", "Cog delta",
       "Cyc base", "Cyc target", "Cyc delta"], rows))

  lines.extend(["", "## Largest complexity-weighted bottleneck changes", ""])
  rows = connection.execute("""
    SELECT name,
           round(base_betweenness_complexity, 5),
           round(target_betweenness_complexity, 5),
           round(betweenness_complexity_delta, 5)
    FROM centrality_delta
    ORDER BY abs(betweenness_complexity_delta) DESC LIMIT 20
  """).fetchall()
  lines.extend(markdown_table(
      ["Function", "Base", "Target", "Delta"], rows))

  lines.extend(["", "## Entrypoint downstream exposure", ""])
  rows = connection.execute("""
    SELECT entrypoint, round(base_cognitive, 4), round(target_cognitive, 4),
           round(cognitive_delta, 4)
    FROM entrypoint_delta
  """).fetchall()
  lines.extend(markdown_table(
      ["Entrypoint", "Base", "Target", "Delta"], rows))

  lines.extend(["", "## Cross-module flow changes", ""])
  rows = connection.execute("""
    SELECT caller_module || ' -> ' || callee_module,
           base_call_sites, target_call_sites, delta
    FROM module_flow_delta
  """).fetchall()
  lines.extend(markdown_table(
      ["Flow", "Base sites", "Target sites", "Delta"], rows))
  (output / "summary.md").write_text("\n".join(lines) + "\n")


def print_summary(connection, output):
  print(f"\nCall-graph diff: {output / 'summary.md'}")
  print("\nTopology")
  for metric, base, target, delta in connection.execute(
      "SELECT metric, base_value, target_value, delta FROM topology"
  ).fetchall():
    print(f"{base:6} -> {target:6}  {delta:+6}  {metric}")
  print("\nLargest complexity-weighted bottleneck changes")
  rows = connection.execute("""
    SELECT name, betweenness_complexity_delta
    FROM centrality_delta
    ORDER BY abs(betweenness_complexity_delta) DESC LIMIT 15
  """).fetchall()
  for name, delta in rows:
    print(f"{delta:+10.5f}  {name}")
  print("\nEntrypoint downstream cognitive exposure")
  for name, base, target, delta in connection.execute("""
    SELECT entrypoint, base_cognitive, target_cognitive, cognitive_delta
    FROM entrypoint_delta
  """).fetchall():
    print(f"{base:8.3f} -> {target:8.3f}  {delta:+8.3f}  {name}")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("base", nargs="?", default="HEAD^")
  parser.add_argument("target", nargs="?", default="HEAD")
  parser.add_argument("-o", "--output", type=pathlib.Path)
  parser.add_argument(
      "--cache-dir", type=pathlib.Path,
      help="content-addressed cache (default: shared Git directory)")
  parser.add_argument(
      "--no-cache", action="store_true",
      help="rerun both analyses without reading or writing the cache")
  args = parser.parse_args()
  base = git("rev-parse", args.base)
  target = git("rev-parse", args.target)
  if base == target:
    raise SystemExit("base and target resolve to the same commit")
  short_base = git("rev-parse", "--short", base)
  short_target = git("rev-parse", "--short", target)
  output = args.output or (
      ROOT / "build-homebrew/callgraph/diffs" /
      f"{short_base}..{short_target}")
  output.mkdir(parents=True, exist_ok=True)
  cache_root = (args.cache_dir.expanduser().resolve()
                if args.cache_dir else default_cache_root())
  use_cache = not args.no_cache
  temporary = pathlib.Path(tempfile.mkdtemp(prefix="moppe-callgraph-diff-"))
  base_worktree = temporary / "base"
  target_worktree = temporary / "target"
  try:
    base_manifest = cache_manifest(base)
    target_manifest = cache_manifest(target)
    if (use_cache and
        base_manifest["cache_key"] == target_manifest["cache_key"]):
      base_db, base_manifest, base_hit = analyze_or_restore(
          base, base_manifest, base_worktree, output / "base-analysis.log",
          cache_root, use_cache)
      target_db, target_hit = base_db, True
      print("Base and target have identical analysis inputs; reusing the "
            "snapshot.", file=sys.stderr)
    else:
      with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        base_future = executor.submit(
            analyze_or_restore, base, base_manifest, base_worktree,
            output / "base-analysis.log", cache_root, use_cache)
        target_future = executor.submit(
            analyze_or_restore, target, target_manifest, target_worktree,
            output / "target-analysis.log", cache_root, use_cache)
        base_db, base_manifest, base_hit = base_future.result()
        target_db, target_manifest, target_hit = target_future.result()
    saved_base_db = output / "base.duckdb"
    shutil.copy2(base_db, saved_base_db)
    remove_worktree(base_worktree)
    saved_target_db = output / "target.duckdb"
    shutil.copy2(target_db, saved_target_db)
    remove_worktree(target_worktree)
    comparison = output / "comparison.duckdb"
    comparison.unlink(missing_ok=True)
    connection = duckdb.connect(str(comparison))
    connection.execute(
        f"ATTACH '{sql_path(saved_base_db)}' AS base (READ_ONLY)")
    connection.execute(
        f"ATTACH '{sql_path(saved_target_db)}' AS target (READ_ONLY)")
    create_comparison(connection)
    for table in (
        "topology", "complexity_delta", "centrality_delta",
        "entrypoint_delta", "module_flow_delta", "signature_changes",
        "added_functions", "removed_functions"):
      export_csv(connection, table, output / f"{table.replace('_', '-')}.csv")
    write_summary(connection, output, short_base, short_target)
    print_summary(connection, output)
    connection.close()
    provenance = {
        "base": {**base_manifest, "cache_hit": base_hit},
        "target": {**target_manifest, "cache_hit": target_hit},
    }
    (output / "provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    if use_cache:
      catalog = refresh_catalog(cache_root, (base_manifest, target_manifest))
      print(f"\nAnalysis cache: {cache_root}")
      print(f"Query catalog:  {catalog}")
  finally:
    for worktree in (base_worktree, target_worktree):
      remove_worktree(worktree)
    shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
  main()

"""Analyze Moppe's call graph with DuckDB, Onager, and directed walks."""

import argparse
import csv
import pathlib
import re
import sys
from collections import defaultdict

import duckdb

from analysis_cache import cache_hit, input_digest, write_manifest


ROOT = pathlib.Path(__file__).resolve().parent.parent
GRAPH = ROOT / "build-homebrew/callgraph"
DEFAULT_OUTPUT = GRAPH / "analysis"
DEFAULT_ENTRYPOINTS = [
    "moppe::game::MoppeGame::tick",
    "moppe::game::MoppeGame::render",
    "moppe::game::build_world",
    "moppe::render::MetalRenderer::end_frame",
]


def sql_path(path):
  return str(path).replace("'", "''")


def load_onager(connection):
  try:
    connection.execute("LOAD onager")
  except duckdb.Error:
    print("Installing the Onager DuckDB extension...", file=sys.stderr)
    connection.execute("INSTALL onager FROM community")
    connection.execute("LOAD onager")


def resolve_entrypoints(nodes, patterns):
  selected = []
  for pattern in patterns:
    exact = [node for node in nodes if node["name"] == pattern]
    matches = exact or [node for node in nodes
                        if re.search(pattern, node["name"], re.IGNORECASE)]
    if len(matches) != 1:
      names = "\n  ".join(node["name"] for node in matches[:20])
      detail = names if names else "no matches"
      raise SystemExit(
          f"entrypoint {pattern!r} matched {len(matches)} nodes:\n  {detail}")
    selected.append(matches[0])
  return selected


def personalized_pagerank(node_ids, edges, seed, damping=0.85,
                          tolerance=1e-10, iterations=1000):
  outgoing = defaultdict(list)
  for source, destination in edges:
    outgoing[source].append(destination)
  personalization = {node_id: 0.0 for node_id in node_ids}
  personalization[seed] = 1.0
  rank = dict(personalization)
  for _ in range(iterations):
    updated = {node_id: (1.0 - damping) * value
               for node_id, value in personalization.items()}
    dangling = 0.0
    for source, value in rank.items():
      destinations = outgoing.get(source)
      if destinations:
        share = damping * value / len(destinations)
        for destination in destinations:
          updated[destination] += share
      else:
        dangling += value
    if dangling:
      updated[seed] += damping * dangling
    difference = sum(abs(updated[node_id] - rank[node_id])
                     for node_id in node_ids)
    rank = updated
    if difference < tolerance:
      break
  return rank


def export_csv(connection, query, path):
  connection.execute(
      f"COPY ({query}) TO '{sql_path(path)}' (HEADER, DELIMITER ',')")


def materialize_graph(connection, nodes_path, edges_path, references_path):
  connection.execute("DROP TABLE IF EXISTS nodes")
  connection.execute("DROP TABLE IF EXISTS edges")
  connection.execute("DROP TABLE IF EXISTS edge_ids")
  connection.execute("DROP TABLE IF EXISTS function_references")
  connection.execute("DROP TABLE IF EXISTS reference_ids")
  connection.execute(f"""
    CREATE TABLE nodes AS
    SELECT row_number() OVER (ORDER BY id)::BIGINT AS node_id, *
    FROM read_csv_auto('{sql_path(nodes_path)}')
  """)
  connection.execute(f"""
    CREATE TABLE edges AS
    SELECT * FROM read_csv_auto('{sql_path(edges_path)}')
  """)
  connection.execute(f"""
    CREATE TABLE function_references AS
    SELECT * FROM read_csv_auto('{sql_path(references_path)}',
                                all_varchar=true)
  """)
  connection.execute("""
    CREATE TABLE edge_ids AS
    SELECT DISTINCT caller.node_id::BIGINT AS src,
                    callee.node_id::BIGINT AS dst
    FROM edges
    JOIN nodes caller ON caller.id = edges.caller_id
    JOIN nodes callee ON callee.id = edges.callee_id
    WHERE edges.resolution = 'project'
    ORDER BY src, dst
  """)
  connection.execute("""
    CREATE TABLE reference_ids AS
    SELECT DISTINCT caller.node_id::BIGINT AS src,
                    referenced.node_id::BIGINT AS dst
    FROM function_references
    JOIN nodes caller ON caller.id = function_references.caller_id
    JOIN nodes referenced
      ON referenced.id = function_references.referenced_id
    ORDER BY src, dst
  """)


def store_liveness(connection, entrypoints):
  connection.execute("DROP TABLE IF EXISTS liveness_edges")
  connection.execute("""
    CREATE TABLE liveness_edges AS
    SELECT src, dst FROM edge_ids
    UNION
    SELECT src, dst FROM reference_ids
    UNION
    SELECT caller.node_id, callee.node_id
    FROM edges unresolved
    JOIN nodes caller ON caller.id = unresolved.caller_id
    JOIN nodes callee
      ON regexp_extract(callee.name, '([^:]+)$', 1) = unresolved.callee
    WHERE unresolved.resolution = 'unresolved'
      AND coalesce(unresolved.callee, '') != ''
    UNION
    SELECT specialization.node_id, template.node_id
    FROM nodes specialization
    JOIN nodes template USING (name, file, line)
    WHERE specialization.node_id != template.node_id
  """)
  connection.execute("DROP TABLE IF EXISTS liveness_roots")
  connection.execute("""
    CREATE TABLE liveness_roots (
      node_id BIGINT PRIMARY KEY,
      reason VARCHAR
    )
  """)
  connection.executemany(
      "INSERT OR IGNORE INTO liveness_roots VALUES (?, 'entrypoint')",
      [(entrypoint["node_id"],) for entrypoint in entrypoints])
  connection.execute("""
    INSERT OR IGNORE INTO liveness_roots
    SELECT node_id, 'program entrypoint'
    FROM nodes
    WHERE name = 'main' AND definition = 1
  """)
  connection.execute("""
    INSERT OR IGNORE INTO liveness_roots
    SELECT node_id, 'implicit C++ runtime boundary'
    FROM nodes
    WHERE definition = 1
      AND (
        kind IN ('constructor', 'destructor', 'conversion_function')
        OR name LIKE '%::operator()'
      )
  """)
  connection.execute("""
    INSERT OR IGNORE INTO liveness_roots
    SELECT node_id, 'virtual dispatch boundary'
    FROM nodes
    WHERE virtual = 1 AND definition = 1
  """)
  connection.execute("""
    INSERT OR IGNORE INTO liveness_roots
    SELECT node_id, 'Objective-C runtime boundary'
    FROM nodes
    WHERE kind IN (
      'objc_class_method_decl', 'objc_instance_method_decl'
    ) AND definition = 1
  """)
  connection.execute("""
    INSERT OR IGNORE INTO liveness_roots
    SELECT referenced.node_id, 'global function reference'
    FROM function_references r
    JOIN nodes referenced ON referenced.id = r.referenced_id
    WHERE coalesce(r.caller_id, '') = ''
      AND referenced.definition = 1
  """)
  connection.execute("DROP TABLE IF EXISTS reachable")
  connection.execute("""
    CREATE TABLE reachable AS
    WITH RECURSIVE walk(node_id) AS (
      SELECT node_id FROM liveness_roots
      UNION
      SELECT edge.dst
      FROM walk
      JOIN liveness_edges edge ON edge.src = walk.node_id
    )
    SELECT node_id FROM walk
  """)
  connection.execute("DROP TABLE IF EXISTS dead_function_candidates")
  connection.execute("""
    CREATE TABLE dead_function_candidates AS
    WITH incoming_calls AS (
      SELECT dst AS node_id, count(*) AS callers
      FROM edge_ids GROUP BY dst
    ), incoming_references AS (
      SELECT dst AS node_id, count(*) AS referencers
      FROM reference_ids GROUP BY dst
    )
    SELECT n.*,
           coalesce(c.callers, 0) AS incoming_callers,
           coalesce(r.referencers, 0) AS incoming_referencers,
           CASE WHEN n.linkage = 'internal'
                THEN 'strong' ELSE 'review' END AS confidence
    FROM nodes n
    LEFT JOIN reachable live USING (node_id)
    LEFT JOIN incoming_calls c USING (node_id)
    LEFT JOIN incoming_references r USING (node_id)
    WHERE n.definition = 1 AND live.node_id IS NULL
    ORDER BY confidence, n.cognitive DESC, n.file, n.line, n.name
  """)


def run_onager(connection):
  connection.execute("""
    CREATE OR REPLACE TABLE pagerank AS
    SELECT * FROM onager_ctr_pagerank(
      (SELECT src, dst FROM edge_ids ORDER BY src, dst),
      damping := 0.85::DOUBLE,
      iterations := 100::BIGINT,
      directed := true
    )
  """)
  connection.execute("""
    CREATE OR REPLACE TABLE betweenness AS
    SELECT * FROM onager_ctr_betweenness(
      (SELECT src, dst FROM edge_ids ORDER BY src, dst)
    )
  """)
  connection.execute("""
    CREATE OR REPLACE TABLE components AS
    SELECT * FROM onager_cmm_components(
      (SELECT src, dst FROM edge_ids ORDER BY src, dst)
    )
  """)
  connection.execute("""
    CREATE OR REPLACE TABLE communities AS
    SELECT * FROM onager_cmm_louvain(
      (SELECT src, dst FROM edge_ids ORDER BY src, dst), seed := 42::BIGINT
    )
  """)


def store_exposure(connection, entrypoints):
  node_rows = connection.execute(
      "SELECT node_id, name, cognitive, cyclomatic FROM nodes").fetchall()
  node_ids = [row[0] for row in node_rows]
  metrics = {row[0]: row[1:] for row in node_rows}
  edges = connection.execute("SELECT src, dst FROM edge_ids").fetchall()
  connection.execute("DROP TABLE IF EXISTS entrypoint_exposure")
  connection.execute("""
    CREATE TABLE entrypoint_exposure (
      entrypoint_id BIGINT,
      entrypoint VARCHAR,
      node_id BIGINT,
      walk_probability DOUBLE,
      cognitive_contribution DOUBLE,
      cyclomatic_contribution DOUBLE
    )
  """)
  output = []
  for entrypoint in entrypoints:
    rank = personalized_pagerank(node_ids, edges, entrypoint["node_id"])
    for node_id, probability in rank.items():
      _, cognitive, cyclomatic = metrics[node_id]
      output.append((
          entrypoint["node_id"], entrypoint["name"], node_id, probability,
          probability * cognitive, probability * cyclomatic))
  connection.executemany(
      "INSERT INTO entrypoint_exposure VALUES (?, ?, ?, ?, ?, ?)", output)


def export_reports(connection, output):
  export_csv(connection, """
    SELECT n.*,
           coalesce(p.rank, 0) AS pagerank,
           coalesce(b.betweenness, 0) AS betweenness,
           coalesce(p.rank, 0) * n.cognitive AS pagerank_x_cognitive,
           coalesce(b.betweenness, 0) * n.cognitive
             AS betweenness_x_cognitive
    FROM nodes n
    LEFT JOIN pagerank p USING (node_id)
    LEFT JOIN betweenness b USING (node_id)
    ORDER BY pagerank_x_cognitive DESC
  """, output / "function-metrics.csv")
  export_csv(connection, """
    SELECT n.*, c.component
    FROM nodes n LEFT JOIN components c USING (node_id)
    ORDER BY c.component, n.name
  """, output / "components.csv")
  export_csv(connection, """
    SELECT n.*, c.community
    FROM nodes n LEFT JOIN communities c USING (node_id)
    ORDER BY c.community, n.name
  """, output / "communities.csv")
  export_csv(connection, """
    SELECT c.community,
           count(*) AS functions,
           sum(n.cognitive) AS cognitive,
           sum(n.cyclomatic) AS cyclomatic,
           sum(coalesce(p.rank, 0)) AS pagerank,
           string_agg(n.name, '; ' ORDER BY coalesce(p.rank, 0) DESC)
             AS members_by_pagerank
    FROM communities c
    JOIN nodes n USING (node_id)
    LEFT JOIN pagerank p USING (node_id)
    GROUP BY c.community
    ORDER BY cognitive DESC
  """, output / "community-summary.csv")
  export_csv(connection, """
    SELECT e.entrypoint, n.name, n.module, n.file, n.line,
           n.cognitive, n.cyclomatic,
           e.walk_probability,
           e.cognitive_contribution,
           e.cyclomatic_contribution
    FROM entrypoint_exposure e
    JOIN nodes n USING (node_id)
    ORDER BY e.entrypoint, e.cognitive_contribution DESC
  """, output / "entrypoint-exposure.csv")
  export_csv(connection, """
    SELECT entrypoint,
           sum(cognitive_contribution) AS total_expected_cognitive,
           sum(cognitive_contribution)
             FILTER (node_id != entrypoint_id) AS downstream_cognitive,
           sum(cyclomatic_contribution) AS total_expected_cyclomatic,
           sum(cyclomatic_contribution)
             FILTER (node_id != entrypoint_id) AS downstream_cyclomatic,
           sum(walk_probability) AS probability_sum
    FROM entrypoint_exposure
    GROUP BY entrypoint
    ORDER BY downstream_cognitive DESC
  """, output / "entrypoint-summary.csv")
  export_csv(connection, """
    SELECT confidence, name, kind, linkage, module, file, line,
           cognitive, cyclomatic, incoming_callers, incoming_referencers
    FROM dead_function_candidates
    ORDER BY CASE confidence WHEN 'strong' THEN 0 ELSE 1 END,
             cognitive DESC, file, line, name
  """, output / "dead-functions.csv")


def print_results(connection, output):
  print(f"\nAnalysis database: {output / 'callgraph.duckdb'}")
  print("\nComplex and globally important")
  rows = connection.execute("""
    SELECT n.name, n.cognitive, p.rank,
           n.cognitive * p.rank AS score
    FROM nodes n JOIN pagerank p USING (node_id)
    WHERE n.cognitive > 0
    ORDER BY score DESC LIMIT 15
  """).fetchall()
  for name, cognitive, rank, score in rows:
    print(f"{score:8.5f}  cog={cognitive:3}  pr={rank:.6f}  {name}")

  print("\nEntrypoint downstream complexity exposure")
  summaries = connection.execute("""
    SELECT entrypoint,
           sum(cognitive_contribution)
             FILTER (node_id != entrypoint_id) AS exposure
    FROM entrypoint_exposure
    GROUP BY entrypoint
    ORDER BY exposure DESC
  """).fetchall()
  for entrypoint, exposure in summaries:
    print(f"{exposure:8.3f}  {entrypoint}")
    contributors = connection.execute("""
      SELECT n.name, e.cognitive_contribution
      FROM entrypoint_exposure e JOIN nodes n USING (node_id)
      WHERE e.entrypoint = ? AND n.cognitive > 0
        AND e.node_id != e.entrypoint_id
      ORDER BY e.cognitive_contribution DESC LIMIT 5
    """, [entrypoint]).fetchall()
    for name, contribution in contributors:
      print(f"           {contribution:8.4f}  {name}")

  strong, review = connection.execute("""
    SELECT count(*) FILTER (confidence = 'strong'),
           count(*) FILTER (confidence = 'review')
    FROM dead_function_candidates
  """).fetchone()
  print("\nPossible dead functions")
  print(f"  {strong} internally linked candidates; {review} require review")
  rows = connection.execute("""
    SELECT name, file, line
    FROM dead_function_candidates
    WHERE confidence = 'strong'
    ORDER BY cognitive DESC, file, line, name
    LIMIT 15
  """).fetchall()
  for name, filename, line in rows:
    print(f"  {filename}:{line}  {name}")
  print(f"  Full report: {output / 'dead-functions.csv'}")


def markdown_name(value):
  return str(value).replace("|", "\\|")


def write_summary(connection, output):
  node_count, edge_count = connection.execute("""
    SELECT (SELECT count(*) FROM nodes),
           (SELECT count(*) FROM edge_ids)
  """).fetchone()
  lines = [
      "# Moppe call-graph analysis",
      "",
      f"Analyzed {node_count} functions and {edge_count} unique project-call "
      "edges. Global graph algorithms come from Onager; entrypoint exposure "
      "uses directed personalized PageRank with damping 0.85.",
      "",
      "## Complex and globally important",
      "",
      "| Function | Cognitive | PageRank | Product |",
      "|---|---:|---:|---:|",
  ]
  rows = connection.execute("""
    SELECT n.name, n.cognitive, p.rank,
           n.cognitive * p.rank AS score
    FROM nodes n JOIN pagerank p USING (node_id)
    WHERE n.cognitive > 0
    ORDER BY score DESC LIMIT 20
  """).fetchall()
  lines.extend(
      f"| {markdown_name(name)} | {cognitive} | {rank:.6f} | {score:.5f} |"
      for name, cognitive, rank, score in rows)

  lines.extend([
      "",
      "## Complex structural bottlenecks",
      "",
      "| Function | Cognitive | Betweenness | Product |",
      "|---|---:|---:|---:|",
  ])
  rows = connection.execute("""
    SELECT n.name, n.cognitive, b.betweenness,
           n.cognitive * b.betweenness AS score
    FROM nodes n JOIN betweenness b USING (node_id)
    WHERE n.cognitive > 0
    ORDER BY score DESC LIMIT 20
  """).fetchall()
  lines.extend(
      f"| {markdown_name(name)} | {cognitive} | {between:.6f} | "
      f"{score:.5f} |"
      for name, cognitive, between, score in rows)

  lines.extend([
      "",
      "## Entrypoint downstream exposure",
      "",
      "| Entrypoint | Expected cognitive | Expected cyclomatic |",
      "|---|---:|---:|",
  ])
  rows = connection.execute("""
    SELECT entrypoint,
           sum(cognitive_contribution)
             FILTER (node_id != entrypoint_id),
           sum(cyclomatic_contribution)
             FILTER (node_id != entrypoint_id)
    FROM entrypoint_exposure
    GROUP BY entrypoint
    ORDER BY 2 DESC
  """).fetchall()
  lines.extend(
      f"| {markdown_name(name)} | {cognitive:.3f} | {cyclomatic:.3f} |"
      for name, cognitive, cyclomatic in rows)

  lines.extend([
      "",
      "## Communities with the most complexity",
      "",
      "Louvain communities are exploratory: Onager may choose a different "
      "valid partition between runs even with a fixed seed.",
      "",
      "| Community | Functions | Cognitive | PageRank | Leading functions |",
      "|---:|---:|---:|---:|---|",
  ])
  rows = connection.execute("""
    SELECT c.community, count(*), sum(n.cognitive),
           sum(coalesce(p.rank, 0)),
           array_to_string(list_slice(
             list(n.name ORDER BY coalesce(p.rank, 0) DESC), 1, 3), '; ')
    FROM communities c
    JOIN nodes n USING (node_id)
    LEFT JOIN pagerank p USING (node_id)
    GROUP BY c.community
    ORDER BY sum(n.cognitive) DESC LIMIT 20
  """).fetchall()
  lines.extend(
      f"| {community} | {functions} | {cognitive} | {rank:.6f} | "
      f"{markdown_name(leaders)} |"
      for community, functions, cognitive, rank, leaders in rows)
  strong, review = connection.execute("""
    SELECT count(*) FILTER (confidence = 'strong'),
           count(*) FILTER (confidence = 'review')
    FROM dead_function_candidates
  """).fetchone()
  lines.extend([
      "",
      "## Possible dead functions",
      "",
      f"The directed liveness walk found {strong} internally linked "
      f"definitions as strong candidates and {review} externally linked "
      "definitions requiring review. It starts at program and configured "
      "entrypoints, virtual methods, Objective-C runtime methods, and global "
      "function references; it follows both calls and function-value "
      "references. Strong means unreachable in this macOS Moppe and Atelier "
      "configuration; tests and other build configurations may still use a "
      "candidate. This is a deletion shortlist rather than proof.",
      "",
      "| Confidence | Function | File | Line | Cognitive |",
      "|---|---|---|---:|---:|",
  ])
  rows = connection.execute("""
    SELECT confidence, name, file, line, cognitive
    FROM dead_function_candidates
    ORDER BY CASE confidence WHEN 'strong' THEN 0 ELSE 1 END,
             cognitive DESC, file, line, name
    LIMIT 40
  """).fetchall()
  lines.extend(
      f"| {confidence} | {markdown_name(name)} | {filename} | {line} | "
      f"{cognitive} |"
      for confidence, name, filename, line, cognitive in rows)
  (output / "summary.md").write_text("\n".join(lines) + "\n")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("-o", "--output", type=pathlib.Path,
                      default=DEFAULT_OUTPUT)
  parser.add_argument("--entrypoint", action="append",
                      help="exact function name or regex; may be repeated")
  parser.add_argument("--refresh", action="store_true",
                      help="ignore a matching cached analysis")
  args = parser.parse_args()
  nodes_path = GRAPH / "nodes.csv"
  edges_path = GRAPH / "edges.csv"
  references_path = GRAPH / "references.csv"
  if (not nodes_path.exists() or not edges_path.exists()
      or not references_path.exists()):
    raise SystemExit("run tools/callgraph-report first")
  args.output.mkdir(parents=True, exist_ok=True)
  database = args.output / "callgraph.duckdb"
  patterns = args.entrypoint or DEFAULT_ENTRYPOINTS
  manifest_path = args.output / "input-manifest.json"
  digest = input_digest(ROOT, [
      nodes_path,
      edges_path,
      ROOT / "tools/analysis_cache.py",
      ROOT / "tools/callgraph-analyze",
      ROOT / "tools/callgraph_analyze.py",
  ], {"analysis": "callgraph-analyze", "entrypoints": patterns, "version": 4})
  required = [
      database,
      args.output / "summary.md",
      args.output / "function-metrics.csv",
      args.output / "entrypoint-exposure.csv",
      args.output / "dead-functions.csv",
  ]
  if not args.refresh and cache_hit(manifest_path, digest, required):
    print("Call-graph analysis inputs are unchanged; reusing cached reports.",
          file=sys.stderr)
    connection = duckdb.connect(str(database), read_only=True)
    print_results(connection, args.output)
    connection.close()
    return

  connection = duckdb.connect(str(database))
  connection.execute("SET threads = 1")
  load_onager(connection)
  materialize_graph(connection, nodes_path, edges_path, references_path)
  run_onager(connection)
  nodes = [dict(zip(
      ("node_id", "name"), row)) for row in
      connection.execute("SELECT node_id, name FROM nodes").fetchall()]
  entrypoints = resolve_entrypoints(nodes, patterns)
  store_liveness(connection, entrypoints)
  store_exposure(connection, entrypoints)
  export_reports(connection, args.output)
  write_summary(connection, args.output)
  print_results(connection, args.output)
  connection.close()
  write_manifest(manifest_path, digest)


if __name__ == "__main__":
  main()

"""Analyze Moppe's call graph with DuckDB, Onager, and directed walks."""

import argparse
import csv
import pathlib
import re
import sys
from collections import defaultdict

import duckdb


ROOT = pathlib.Path(__file__).resolve().parent.parent
GRAPH = ROOT / "build-homebrew/callgraph"
DEFAULT_OUTPUT = GRAPH / "analysis"
DEFAULT_ENTRYPOINTS = [
    "moppe::game::MoppeGame::tick",
    "moppe::game::MoppeGame::render",
    "moppe::game::MoppeGame::generate_world_inner",
    "moppe::game::TerrainLab::tick",
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


def materialize_graph(connection, nodes_path, edges_path):
  connection.execute("DROP TABLE IF EXISTS nodes")
  connection.execute("DROP TABLE IF EXISTS edges")
  connection.execute("DROP TABLE IF EXISTS edge_ids")
  connection.execute(f"""
    CREATE TABLE nodes AS
    SELECT row_number() OVER (ORDER BY id)::BIGINT AS node_id, *
    FROM read_csv_auto('{sql_path(nodes_path)}')
  """)
  connection.execute(f"""
    CREATE TABLE edges AS
    SELECT * FROM read_csv_auto('{sql_path(edges_path)}')
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
  (output / "summary.md").write_text("\n".join(lines) + "\n")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("-o", "--output", type=pathlib.Path,
                      default=DEFAULT_OUTPUT)
  parser.add_argument("--entrypoint", action="append",
                      help="exact function name or regex; may be repeated")
  args = parser.parse_args()
  nodes_path = GRAPH / "nodes.csv"
  edges_path = GRAPH / "edges.csv"
  if not nodes_path.exists() or not edges_path.exists():
    raise SystemExit("run tools/callgraph-report first")
  args.output.mkdir(parents=True, exist_ok=True)
  database = args.output / "callgraph.duckdb"
  connection = duckdb.connect(str(database))
  connection.execute("SET threads = 1")
  load_onager(connection)
  materialize_graph(connection, nodes_path, edges_path)
  run_onager(connection)
  nodes = [dict(zip(
      ("node_id", "name"), row)) for row in
      connection.execute("SELECT node_id, name FROM nodes").fetchall()]
  patterns = args.entrypoint or DEFAULT_ENTRYPOINTS
  entrypoints = resolve_entrypoints(nodes, patterns)
  store_exposure(connection, entrypoints)
  export_reports(connection, args.output)
  write_summary(connection, args.output)
  print_results(connection, args.output)
  connection.close()


if __name__ == "__main__":
  main()

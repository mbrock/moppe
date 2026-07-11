"""Extract Moppe's static call graph using Clang's C API."""

import argparse
import csv
import gc
import json
import multiprocessing
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
from collections import Counter, defaultdict, deque

from clang import cindex


ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-homebrew"
DEFAULT_OUTPUT = BUILD / "callgraph"
FUNCTION_KINDS = {
    cindex.CursorKind.FUNCTION_DECL,
    cindex.CursorKind.CXX_METHOD,
    cindex.CursorKind.CONSTRUCTOR,
    cindex.CursorKind.DESTRUCTOR,
    cindex.CursorKind.CONVERSION_FUNCTION,
    cindex.CursorKind.FUNCTION_TEMPLATE,
}
CALL_KINDS = {
    cindex.CursorKind.CALL_EXPR,
    cindex.CursorKind.CXX_NEW_EXPR,
    cindex.CursorKind.CXX_DELETE_EXPR,
    cindex.CursorKind.OBJC_MESSAGE_EXPR,
}


def run(command, **kwargs):
  display = command
  if len(command) > 12:
    display = [*command[:5], "...", f"({len(command)} arguments)"]
  print("+ " + " ".join(map(str, display)), file=sys.stderr)
  return subprocess.run(command, check=True, **kwargs)


def relative(path):
  try:
    return pathlib.Path(path).resolve().relative_to(ROOT).as_posix()
  except ValueError:
    return pathlib.Path(path).as_posix()


def project_location(cursor):
  location = cursor.location
  if not location or not location.file:
    return None
  path = pathlib.Path(location.file.name).resolve()
  try:
    path.relative_to(ROOT / "moppe")
  except ValueError:
    return None
  return relative(path), location.line, location.column


def qualified_name(cursor):
  parts = []
  current = cursor
  while current and current.kind != cindex.CursorKind.TRANSLATION_UNIT:
    if current.spelling:
      parts.append(current.spelling)
    current = current.semantic_parent
  return "::".join(reversed(parts))


def cursor_id(cursor):
  usr = cursor.get_usr()
  location = project_location(cursor)
  if usr and cursor.spelling == "main" and location:
    return f"{usr}@{location[0]}"
  if usr:
    return usr
  if location:
    return f"location:{location[0]}:{location[1]}:{qualified_name(cursor)}"
  return ""


def module_for(filename):
  parts = pathlib.PurePosixPath(filename).parts
  return "/".join(parts[:2]) if len(parts) >= 2 else "external"


def node_for(cursor):
  location = project_location(cursor)
  if not location:
    return None
  node_id = cursor_id(cursor)
  if not node_id:
    return None
  filename, line, _ = location
  return {
      "id": node_id,
      "name": qualified_name(cursor),
      "kind": cursor.kind.name.lower(),
      "module": module_for(filename),
      "file": filename,
      "line": line,
      "definition": int(cursor.is_definition()),
      "cyclomatic": 0,
      "cognitive": 0,
  }


def parse_arguments(entry):
  if "arguments" in entry:
    arguments = list(entry["arguments"])[1:]
  else:
    arguments = shlex.split(entry["command"])[1:]
  source = pathlib.Path(entry["file"]).resolve()
  resource_dir = os.environ["LIBCLANG_RESOURCE_DIR"]
  result = [f"-resource-dir={resource_dir}"]
  skip = False
  for argument in arguments:
    if skip:
      skip = False
      continue
    if argument == "-o":
      skip = True
    elif argument == "-c" or pathlib.Path(argument).resolve() == source:
      continue
    else:
      result.append(argument)
  return result


def load_complexity(sources, refresh, jobs):
  report = BUILD / "complexity.csv"
  stale = not report.exists()
  if not stale:
    stale = any(source.stat().st_mtime > report.stat().st_mtime
                for source in sources)
  if refresh or stale:
    run([str(ROOT / "tools/complexity-report"), "--top", "0",
         "--jobs", str(jobs)], cwd=ROOT)
  values = {}
  with report.open(newline="") as input_file:
    for row in csv.DictReader(input_file):
      values[(row["file"], int(row["line"]))] = (
          int(row["cyclomatic"]), int(row["cognitive"]))
  return values


def dispatch_kind(reference):
  if not reference or reference.kind not in FUNCTION_KINDS:
    return "indirect"
  if (reference.kind == cindex.CursorKind.CXX_METHOD
      and reference.is_virtual_method()):
    return "virtual"
  return "direct"


def extract_translation_unit(index, entry, nodes, edges):
  source = pathlib.Path(entry["file"]).resolve()
  source_name = relative(source)
  arguments = parse_arguments(entry)
  translation_unit = index.parse(
      str(source), args=arguments, options=0)
  errors = [diagnostic for diagnostic in translation_unit.diagnostics
            if diagnostic.severity >= cindex.Diagnostic.Error]
  if errors:
    for diagnostic in errors[:10]:
      print(diagnostic, file=sys.stderr)
    raise RuntimeError(f"Clang reported errors while parsing {source}")

  def visit(cursor, caller=None):
    location = project_location(cursor)
    if cursor.kind != cindex.CursorKind.TRANSLATION_UNIT:
      if not location or location[0] != source_name:
        return

    active_caller = caller
    if cursor.kind in FUNCTION_KINDS:
      node = node_for(cursor)
      if node:
        previous = nodes.get(node["id"])
        if not previous or node["definition"] > previous["definition"]:
          nodes[node["id"]] = node
        active_caller = node["id"]

    if cursor.kind in CALL_KINDS and caller:
      reference = cursor.referenced
      callee = node_for(reference) if reference else None
      if callee:
        previous = nodes.get(callee["id"])
        if not previous or callee["definition"] > previous["definition"]:
          nodes[callee["id"]] = callee
      filename, line, column = location
      edges.append({
          "caller_id": caller,
          "callee_id": callee["id"] if callee else "",
          "caller": nodes[caller]["name"],
          "callee": callee["name"] if callee else cursor.displayname,
          "file": filename,
          "line": line,
          "column": column,
          "dispatch": dispatch_kind(reference),
          "resolution": ("project" if callee else
                         "external" if reference else "unresolved"),
      })

    for child in cursor.get_children():
      visit(child, active_caller)

  visit(translation_unit.cursor)
  del visit
  translation_unit = None
  gc.collect()


def extract_entry(entry):
  cindex.Config.set_library_file(os.environ["LIBCLANG_LIBRARY_FILE"])
  nodes = {}
  edges = []
  index = cindex.Index.create()
  extract_translation_unit(index, entry, nodes, edges)
  return nodes, edges


def write_csv(path, rows, fields):
  with path.open("w", newline="") as output_file:
    writer = csv.DictWriter(output_file, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)


def select_focus(nodes, pattern):
  exact = [node_id for node_id, node in nodes.items()
           if node["name"] == pattern]
  if len(exact) == 1:
    return exact[0]
  regex = re.compile(pattern, re.IGNORECASE)
  matches = [node_id for node_id, node in nodes.items()
             if regex.search(node["name"])]
  if len(matches) != 1:
    names = sorted(nodes[node_id]["name"] for node_id in matches)[:20]
    detail = "\n  ".join(names) if names else "no matches"
    raise SystemExit(
        f"--focus must identify one function; found {len(matches)}:\n  {detail}")
  return matches[0]


def focused_nodes(focus, edges, depth):
  outgoing = defaultdict(set)
  incoming = defaultdict(set)
  for edge in edges:
    if edge["callee_id"]:
      outgoing[edge["caller_id"]].add(edge["callee_id"])
      incoming[edge["callee_id"]].add(edge["caller_id"])
  selected = {focus}
  queue = deque([(focus, 0)])
  while queue:
    node_id, distance = queue.popleft()
    if distance >= depth:
      continue
    for adjacent in outgoing[node_id] | incoming[node_id]:
      if adjacent not in selected:
        selected.add(adjacent)
        queue.append((adjacent, distance + 1))
  return selected


def write_dot(path, nodes, edges, focus, depth):
  selected = focused_nodes(focus, edges, depth)
  identifiers = {node_id: f"n{index}"
                 for index, node_id in enumerate(sorted(selected))}
  colors = {
      "moppe/game": "lightblue",
      "moppe/gfx": "gray90",
      "moppe/map": "thistle",
      "moppe/mov": "mistyrose",
      "moppe/platform": "lightcyan",
      "moppe/render": "palegreen",
      "moppe/terrain": "khaki1",
  }
  lines = ["digraph callgraph {", "  rankdir=LR;", "  node [shape=box];"]
  for node_id in sorted(selected):
    node = nodes[node_id]
    complexity = (f'cog {node["cognitive"]}, cyc {node["cyclomatic"]}'
                  if node["cyclomatic"] else "complexity n/a")
    label = (f'{node["name"]}\n{node["file"]}:{node["line"]}'
             f'\n{complexity}')
    label = (label.replace("\\", "\\\\").replace('"', '\\"')
             .replace("\n", "\\n"))
    color = colors.get(node["module"], "white")
    attributes = [f'label="{label}"', "style=filled",
                  f'fillcolor="{color}"']
    if node_id == focus:
      attributes[-1] = 'fillcolor="gold"'
    lines.append(f'  {identifiers[node_id]} [{", ".join(attributes)}];')
  seen = set()
  for edge in edges:
    pair = (edge["caller_id"], edge["callee_id"], edge["dispatch"])
    if pair in seen or not set(pair[:2]) <= selected:
      continue
    seen.add(pair)
    style = "dashed" if edge["dispatch"] != "direct" else "solid"
    label = (f', label="{edge["dispatch"]}"'
             if edge["dispatch"] != "direct" else "")
    lines.append(
        f'  {identifiers[pair[0]]} -> {identifiers[pair[1]]} '
        f'[style={style}{label}];')
  lines.append("}")
  path.write_text("\n".join(lines) + "\n")


def print_summary(nodes, edges):
  resolved = [edge for edge in edges if edge["callee_id"]]
  outgoing = defaultdict(set)
  incoming = defaultdict(set)
  for edge in resolved:
    outgoing[edge["caller_id"]].add(edge["callee_id"])
    incoming[edge["callee_id"]].add(edge["caller_id"])
  print(f"\nWrote {len(nodes)} nodes and {len(edges)} call sites")
  print("\nHighest fan-out")
  for node_id, callees in sorted(
      outgoing.items(), key=lambda item: -len(item[1]))[:10]:
    print(f"{len(callees):4}  {nodes[node_id]['name']}")
  print("\nHighest fan-in")
  for node_id, callers in sorted(
      incoming.items(), key=lambda item: -len(item[1]))[:10]:
    print(f"{len(callers):4}  {nodes[node_id]['name']}")

  modules = Counter()
  for edge in resolved:
    caller = nodes[edge["caller_id"]]["module"]
    callee = nodes[edge["callee_id"]]["module"]
    if caller != callee:
      modules[(caller, callee)] += 1
  print("\nCross-module call sites")
  for (caller, callee), count in modules.most_common():
    print(f"{count:4}  {caller} -> {callee}")


def write_module_edges(path, nodes, edges):
  groups = {}
  for edge in edges:
    if not edge["callee_id"]:
      continue
    caller = nodes[edge["caller_id"]]["module"]
    callee = nodes[edge["callee_id"]]["module"]
    if caller == callee:
      continue
    group = groups.setdefault((caller, callee), {
        "call_sites": 0, "callers": set(), "callees": set()})
    group["call_sites"] += 1
    group["callers"].add(edge["caller_id"])
    group["callees"].add(edge["callee_id"])
  rows = [{
      "caller_module": caller,
      "callee_module": callee,
      "call_sites": group["call_sites"],
      "unique_callers": len(group["callers"]),
      "unique_callees": len(group["callees"]),
  } for (caller, callee), group in groups.items()]
  rows.sort(key=lambda row: -row["call_sites"])
  fields = ["caller_module", "callee_module", "call_sites",
            "unique_callers", "unique_callees"]
  write_csv(path, rows, fields)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("-o", "--output", type=pathlib.Path,
                      default=DEFAULT_OUTPUT)
  parser.add_argument("--focus", help="function name or regular expression")
  parser.add_argument("--source", help="only analyze source paths matching regex")
  parser.add_argument("--depth", type=int, default=2,
                      help="focused graph traversal depth (default: 2)")
  parser.add_argument("-j", "--jobs", type=int, default=4,
                      help="parallel Clang processes (default: 4)")
  parser.add_argument("--refresh-complexity", action="store_true")
  args = parser.parse_args()

  library = os.environ.get("LIBCLANG_LIBRARY_FILE")
  if not library:
    raise SystemExit("LIBCLANG_LIBRARY_FILE is not set; use callgraph-report")
  run(["cmake", "--preset", "homebrew-llvm"], cwd=ROOT)

  commands = json.loads((BUILD / "compile_commands.json").read_text())
  entries = {}
  for entry in commands:
    source = pathlib.Path(entry["file"]).resolve()
    try:
      source.relative_to(ROOT / "moppe")
    except ValueError:
      continue
    if source.suffix in (".cc", ".cpp", ".mm"):
      entries[source] = entry
  sources = sorted(entries)
  if args.source:
    source_pattern = re.compile(args.source)
    sources = [source for source in sources
               if source_pattern.search(relative(source))]
    entries = {source: entries[source] for source in sources}
  if not sources:
    raise SystemExit("no source files matched")
  complexity = load_complexity(sources, args.refresh_complexity, args.jobs)

  nodes = {}
  edges = []
  context = multiprocessing.get_context("spawn")
  pool = context.Pool(processes=args.jobs, maxtasksperchild=1)
  try:
    results = pool.imap_unordered(
        extract_entry, (entries[source] for source in sources))
    for number, (source_nodes, source_edges) in enumerate(results, 1):
      print(f"[{number:2}/{len(sources)}] translation units", file=sys.stderr)
      for node_id, node in source_nodes.items():
        previous = nodes.get(node_id)
        if not previous or node["definition"] > previous["definition"]:
          nodes[node_id] = node
      edges.extend(source_edges)
  finally:
    pool.close()
    pool.join()

  for node in nodes.values():
    metrics = complexity.get((node["file"], node["line"]), (0, 0))
    node["cyclomatic"], node["cognitive"] = metrics
  edge_fields = list(edges[0])
  edges = [dict(zip(edge_fields, values)) for values in {
      tuple(edge[field] for field in edge_fields) for edge in edges}]
  edges = sorted(edges, key=lambda edge: (
      edge["caller"], edge["file"], edge["line"], edge["column"]))
  args.output.mkdir(parents=True, exist_ok=True)
  write_csv(args.output / "nodes.csv", sorted(nodes.values(),
            key=lambda node: (node["file"], node["line"], node["name"])),
            list(next(iter(nodes.values()))))
  write_csv(args.output / "edges.csv", edges, list(edges[0]))
  write_module_edges(args.output / "module-edges.csv", nodes, edges)
  print_summary(nodes, edges)

  if args.focus:
    focus = select_focus(nodes, args.focus)
    dot_path = args.output / "focus.dot"
    svg_path = args.output / "focus.svg"
    write_dot(dot_path, nodes, edges, focus, args.depth)
    if shutil.which("dot"):
      run(["dot", "-Tsvg", str(dot_path), "-o", str(svg_path)])
      print(f"\nFocused graph: {svg_path}")
    else:
      print(f"\nFocused graph: {dot_path}")
  else:
    (args.output / "focus.dot").unlink(missing_ok=True)
    (args.output / "focus.svg").unlink(missing_ok=True)


if __name__ == "__main__":
  main()

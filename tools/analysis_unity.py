"""Fold CMake's target-level unity files into analysis-level batches."""

import json
import pathlib
import re
import shlex


SOURCE_SUFFIXES = (".cc", ".cpp", ".mm")
INCLUDE_RE = re.compile(r'^#include "([^"]+)"$')


def command_arguments(entry):
  if "arguments" in entry:
    return list(entry["arguments"])
  return shlex.split(entry["command"])


def original_sources(entry, root, build):
  source = pathlib.Path(entry["file"]).resolve()
  if "/Unity/" in source.as_posix() and source.is_relative_to(build):
    sources = []
    for line in source.read_text().splitlines():
      match = INCLUDE_RE.match(line)
      if match:
        included = pathlib.Path(match.group(1)).resolve()
        if (included.suffix in SOURCE_SUFFIXES
            and included.is_relative_to(root / "moppe")):
          sources.append(included)
    return sources
  if (source.suffix in SOURCE_SUFFIXES
      and source.is_relative_to(root / "moppe")):
    return [source]
  return []


def compile_environment(entry, objective_cpp_define):
  source = pathlib.Path(entry["file"]).resolve()
  arguments = command_arguments(entry)
  result = []
  skip = False
  for index, argument in enumerate(arguments):
    if index == 0:
      result.append(argument)
    elif skip:
      skip = False
    elif argument == "-o":
      skip = True
    elif argument.startswith("-DMOPPE_SHADER_NAME="):
      continue
    elif argument == "-c" or pathlib.Path(argument).resolve() == source:
      continue
    else:
      result.append(argument)
  if objective_cpp_define and "objective-c++" in result:
    result.append(objective_cpp_define)
  return tuple(result)


def analysis_entries(commands, root, build):
  objective_cpp_define = next((
      argument for entry in commands for argument in command_arguments(entry)
      if argument.startswith("-DMOPPE_SHADER_NAME=")), None)
  groups = {}
  for entry in commands:
    sources = original_sources(entry, root, build)
    if sources:
      environment = compile_environment(entry, objective_cpp_define)
      groups.setdefault(environment, set()).update(sources)

  output = build / "analysis-unity"
  output.mkdir(parents=True, exist_ok=True)
  entries = []
  for number, (environment, sources) in enumerate(sorted(groups.items()), 1):
    sources = sorted(sources)
    objective_cpp = any(source.suffix == ".mm" for source in sources)
    suffix = ".mm" if objective_cpp else ".cc"
    batch = output / f"batch-{number}{suffix}"
    batch.write_text(
        "/* Generated source-analysis unity batch. */\n\n"
        + "\n\n".join(
            f'#include "{source}"' for source in sources)
        + "\n")
    arguments = [*environment, "-c", str(batch)]
    entries.append({
        "directory": str(root),
        "file": str(batch),
        "arguments": arguments,
        "_moppe_sources": [
            source.relative_to(root).as_posix() for source in sources],
    })

  compile_commands = [{key: value for key, value in entry.items()
                       if not key.startswith("_")}
                      for entry in entries]
  (output / "compile_commands.json").write_text(
      json.dumps(compile_commands, indent=2) + "\n")
  return entries

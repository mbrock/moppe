"""Small content-based cache helpers for Moppe's source-analysis tools."""

import hashlib
import json
import pathlib
import subprocess


SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".m", ".mm",
}


def repository_inputs(root, tool_paths):
  result = subprocess.run(
      ["git", "-C", str(root), "ls-files", "--cached", "--others",
       "--exclude-standard", "-z"],
      check=True, stdout=subprocess.PIPE)
  paths = []
  for value in result.stdout.split(b"\0"):
    if not value:
      continue
    relative = pathlib.PurePosixPath(value.decode())
    path = root / relative
    if not path.is_file():
      continue
    if (relative.parts and relative.parts[0] == "moppe"
        and relative.suffix.lower() in SOURCE_SUFFIXES):
      paths.append(path)
    elif (relative.name == "CMakeLists.txt"
          or relative.name.startswith("CMakePresets")
          or (relative.parts and relative.parts[0] == "cmake")):
      paths.append(path)
  paths.extend(root / path for path in tool_paths)
  return sorted(set(path.resolve() for path in paths if path.is_file()))


def input_digest(root, paths, identity):
  digest = hashlib.sha256()
  digest.update(json.dumps(
      identity, sort_keys=True, separators=(",", ":")).encode())
  digest.update(b"\0")
  for path in sorted(set(path.resolve() for path in paths)):
    try:
      name = path.relative_to(root).as_posix()
    except ValueError:
      name = path.as_posix()
    digest.update(name.encode())
    digest.update(b"\0")
    digest.update(path.read_bytes())
    digest.update(b"\0")
  return digest.hexdigest()


def repository_digest(root, tool_paths, identity):
  return input_digest(
      root, repository_inputs(root, tool_paths), identity)


def cache_hit(manifest_path, digest, required_paths):
  if not all(path.is_file() for path in required_paths):
    return False
  try:
    manifest = json.loads(manifest_path.read_text())
  except (FileNotFoundError, json.JSONDecodeError):
    return False
  return manifest.get("input_digest") == digest


def write_manifest(path, digest):
  path.write_text(json.dumps(
      {"input_digest": digest}, indent=2, sort_keys=True) + "\n")

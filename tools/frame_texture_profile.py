# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "matplotlib"]
# ///
"""Per-distance-band texture statistics for gazetteer frames.

Distance rendering cannot be judged by impression: what is absent in the
far field is exactly what an eye (or an agent) skims past in a still.
This instrument turns a frame into curves. Each image row of a
grass-gradient frame maps monotonically to a ground distance through the
camera pose recorded in gazetteer.csv, so per-row gradient statistics
become texture-energy-versus-distance profiles, and a vegetation
representation that dies early is a cliff in a curve rather than a
subtlety.

Two headline scalars per frame and build, and one warning. floor-merge:
the last distance at which vertical-structure energy (|dI/dx|, the
signature of upright blades) still exceeds the far-field floor — how far
ANY representation persists. mid-hold: mean energy in the 30-70 m band
relative to the near field — how much of the near texture survives the
mid-field. The warning: energy measures CONTRAST, not quality. A band of
sparse thin blades against bare substrate scores high while being
exactly the artifact (the calibration run rated the pre-fix pop-in band
above the fix). The scalars locate change; the overlaid per-build curves
and the anisotropy column (|dx|/|dy|, strokes versus salt-and-pepper)
decide its sign.

Usage:
  tools/frame-texture-profile OUT_DIR LABEL=CAPTURE_DIR [LABEL=DIR ...]
        [--frames NAME[,NAME...]]

Writes OUT_DIR/profile.csv, one overlay plot per analyzed frame, and
prints the texture-horizon table.
"""

import argparse
import csv
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

DEFAULT_FRAMES = [
    "grass-gradient-sunward",
    "grass-gradient-crosslit",
    "grass-gradient-antisun",
    "grass-gradient-down",
]

MAX_DISTANCE_M = 200.0
NEAR_BAND_M = (6.0, 16.0)


def read_gazetteer_row(capture_dir: Path, frame_name: str) -> dict:
    with open(capture_dir / "gazetteer.csv", newline="") as handle:
        for row in csv.DictReader(handle):
            if row.get("name") == frame_name:
                return row
    raise KeyError(f"{frame_name} not found in {capture_dir}/gazetteer.csv")


def row_distances(meta: dict, height_px: int) -> np.ndarray:
    """Slant distance to locally planar ground for each image row centre.

    Rows at or above the horizon get +inf. The gazetteer camera is level
    (no roll), so a row is an iso-pitch line and the mapping is exact for
    flat turf, approximate on relief.
    """
    fov_v = math.radians(float(meta["vertical_fov_deg"]))
    focal_px = (height_px / 2.0) / math.tan(fov_v / 2.0)
    eye = np.array([float(meta["eye_x_m"]), float(meta["eye_y_m"]),
                    float(meta["eye_z_m"])])
    subject = np.array([float(meta["subject_x_m"]), float(meta["subject_y_m"]),
                        float(meta["subject_z_m"])])
    forward = subject - eye
    pitch = math.asin(forward[1] / np.linalg.norm(forward))
    clearance = float(meta["camera_clearance_m"])

    rows = np.arange(height_px) + 0.5
    row_pitch = pitch + np.arctan((height_px / 2.0 - rows) / focal_px)
    sin_down = -np.sin(row_pitch)
    distances = np.full(height_px, np.inf)
    ground = sin_down > 1e-4
    distances[ground] = clearance / sin_down[ground]
    distances[distances > MAX_DISTANCE_M] = np.inf
    return distances


def frame_statistics(image_path: Path) -> dict[str, np.ndarray]:
    """Per-row mean texture energies over the full-resolution frame."""
    rgb = np.asarray(Image.open(image_path).convert("RGB"), dtype=np.float32)
    rgb /= 255.0
    luma = rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)

    grad_x = np.abs(np.diff(luma, axis=1))
    grad_y = np.abs(np.diff(luma, axis=0))
    lapl = np.abs(
        luma[1:-1, 1:-1] * 4.0
        - luma[:-2, 1:-1] - luma[2:, 1:-1]
        - luma[1:-1, :-2] - luma[1:-1, 2:])

    return {
        "grad_x": grad_x.mean(axis=1),
        "grad_y": grad_y[:, :].mean(axis=1),
        "laplacian": np.pad(lapl.mean(axis=1), (1, 1), mode="edge"),
        "luma": luma.mean(axis=1),
    }


def band_edges() -> np.ndarray:
    return np.geomspace(2.5, MAX_DISTANCE_M, 33)


def band_profile(distances: np.ndarray,
                 stats: dict[str, np.ndarray]) -> list[dict]:
    edges = band_edges()
    rows = []
    height = len(distances)
    for lo, hi in zip(edges[:-1], edges[1:]):
        member = (distances >= lo) & (distances < hi)
        member[: 2] = False
        member[height - 2:] = False
        if member.sum() < 3:
            continue
        entry = {"distance_m": math.sqrt(lo * hi), "rows": int(member.sum())}
        for key, series in stats.items():
            length = len(series)
            entry[key] = float(series[member[:length]].mean())
        entry["anisotropy"] = entry["grad_x"] / max(entry["grad_y"], 1e-6)
        rows.append(entry)
    return rows


def floor_merge(profile: list[dict], key: str = "grad_x") -> float:
    """Last distance where energy exceeds the far floor by 20%: how far
    any representation persists before only substrate remains."""
    if len(profile) < 6:
        return float("nan")
    floor = float(np.mean([p[key] for p in profile[-3:]]))
    threshold = floor * 1.2
    merged = profile[-1]["distance_m"]
    for point in reversed(profile[:-3]):
        if point[key] > threshold:
            return point["distance_m"]
        merged = point["distance_m"]
    return merged


def mid_hold(profile: list[dict], key: str = "grad_x") -> float:
    """Mean 30-70 m energy over the near-field level. High values can be
    surviving texture OR an artifact band; the curves decide which."""
    near = [p[key] for p in profile
            if NEAR_BAND_M[0] <= p["distance_m"] <= NEAR_BAND_M[1]]
    mid = [p[key] for p in profile if 30.0 <= p["distance_m"] <= 70.0]
    if not near or not mid:
        return float("nan")
    return float(np.mean(mid) / np.mean(near))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("captures", nargs="+",
                        help="LABEL=CAPTURE_DIR (gazetteer output directory)")
    parser.add_argument("--frames", default=",".join(DEFAULT_FRAMES))
    args = parser.parse_args()

    frame_names = [f.strip() for f in args.frames.split(",") if f.strip()]
    labelled = []
    for spec in args.captures:
        label, _, path = spec.partition("=")
        if not path:
            label, path = Path(spec).name, spec
        labelled.append((label, Path(path)))

    args.out_dir.mkdir(parents=True, exist_ok=True)
    all_rows = []
    horizons: dict[str, dict[str, float]] = {}

    for frame_name in frame_names:
        profiles = {}
        for label, capture in labelled:
            meta = read_gazetteer_row(capture, frame_name)
            image_path = capture / meta["filename"]
            stats = frame_statistics(image_path)
            distances = row_distances(meta, len(stats["luma"]))
            profile = band_profile(distances, stats)
            profiles[label] = profile
            horizons.setdefault(frame_name, {})[label] = (
                floor_merge(profile), mid_hold(profile))
            for point in profile:
                all_rows.append({"frame": frame_name, "build": label, **point})
        plot_frame(args.out_dir, frame_name, profiles)

    with open(args.out_dir / "profile.csv", "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(all_rows[0].keys()))
        writer.writeheader()
        writer.writerows(all_rows)

    print(f"{'frame':28s}" + "".join(f"{label:>20s}"
                                     for label, _ in labelled))
    for frame_name in frame_names:
        cells = ""
        for label, _ in labelled:
            merge_m, hold = horizons[frame_name].get(
                label, (float("nan"), float("nan")))
            cells += f"{merge_m:10.0f} m /{hold:5.2f}"
        print(f"{frame_name:28s}{cells}")
    print("\nper cell: floor-merge distance (how far any representation "
          "persists) / mid-hold\n(30-70 m energy over near-field; artifact "
          "bands also score high -- read the curves).\n"
          f"plots and profile.csv in {args.out_dir}")
    return 0


def plot_frame(out_dir: Path, frame_name: str, profiles: dict) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(1, 2, figsize=(12, 4.2))
    for label, profile in profiles.items():
        distance = [p["distance_m"] for p in profile]
        axes[0].plot(distance, [p["grad_x"] for p in profile],
                     marker=".", label=label)
        axes[1].plot(distance, [p["laplacian"] for p in profile],
                     marker=".", label=label)
    for axis, title in zip(axes, ("|dI/dx| (vertical structure)",
                                  "|laplacian| (fine detail)")):
        axis.set_xscale("log")
        axis.set_xlabel("ground distance (m)")
        axis.set_title(f"{frame_name}: {title}")
        axis.grid(True, which="both", alpha=0.3)
        axis.legend()
    figure.tight_layout()
    figure.savefig(out_dir / f"{frame_name}.png", dpi=110)
    plt.close(figure)


if __name__ == "__main__":
    sys.exit(main())

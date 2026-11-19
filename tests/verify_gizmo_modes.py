#!/usr/bin/env python3
"""Headless regression for move, rotate and scale 3D gizmo rendering."""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError("Screenshot is not P6 PPM")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Screenshot bit depth incorrect")
        pixels = stream.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError("Screenshot data length invalid")
    return (width, height), pixels


def capture(xvfb, binary, directory, mode, snapping):
    name = f"v17-{mode}-snap{int(snapping)}.ppm"
    path = directory / name
    args = [
        xvfb, "-a", binary,
        "--gui", "1", "--gui-select", "13", "--gizmo", "1",
        "--gizmo-mode", mode, "--gizmo-snap", str(int(snapping)),
        "--gizmo-grid", "0.5",
        "--frames", "2", "--sim-dt", "0", "--lights", "0",
        "--sun-cascades", "0", "--point-shadows", "0",
        "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
        "--bloom", "0", "--stats", "--screenshot", str(path),
    ]
    result = subprocess.run(args, check=True, capture_output=True,
                            text=True, timeout=120)
    expected_mode = {"move": 0, "rotate": 1, "scale": 2}[mode]
    for key, expected in [
        ("guiVisible", 1), ("gizmoEnabled", 1),
        ("gizmoMode", expected_mode), ("gizmoSnap", int(snapping)),
        ("sceneEntities", 26), ("sceneRenderables", 26),
        ("editorUndoCount", 0), ("editorRedoCount", 0),
    ]:
        match = re.search(r"\b" + re.escape(key) + r"=(\d+)", result.stdout)
        if not match or int(match.group(1)) != expected:
            raise AssertionError(f"Expected {key}={expected}: {result.stdout}")
    return ppm(path)


def changed(first, second, title, min_mean, min_channels):
    if first[0] != second[0]:
        raise AssertionError("Gizmo captures have incompatible sizes")
    differences = [abs(a - b) for a, b in zip(first[1], second[1])]
    mean = sum(differences) / len(differences)
    significant = sum(delta >= 8 for delta in differences)
    print(f"{title}: mean RGB delta={mean:.4f}, "
          f"channels changed>=8={significant}")
    if mean < min_mean or significant < min_channels:
        raise AssertionError(f"{title}: gizmo UI did not visibly change")


def main():
    xvfb, binary, output = sys.argv[1:]
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=True)
    move = capture(xvfb, binary, directory, "move", False)
    rotate = capture(xvfb, binary, directory, "rotate", False)
    scale = capture(xvfb, binary, directory, "scale", False)
    snapped = capture(xvfb, binary, directory, "move", True)

    changed(move, rotate, "Move handles vs projected rotate rings",
            0.03, 300)
    changed(move, scale, "Move arrow heads vs scale squares", 0.005, 40)
    changed(move, snapped, "Snap enabled and grid step control", 0.02, 200)

    for options in [
        ["--gizmo-mode", "invalid"],
        ["--gizmo-grid", "0"],
        ["--gizmo-snap", "2"],
    ]:
        args = [xvfb, "-a", binary, "--frames", "1", *options]
        result = subprocess.run(args, capture_output=True, text=True,
                                timeout=120)
        if result.returncode == 0:
            raise AssertionError(f"Invalid gizmo CLI options accepted: {options}")
    print("All three gizmo modes, grid snap and argument validation passed")


if __name__ == "__main__":
    main()

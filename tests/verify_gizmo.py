#!/usr/bin/env python3
"""Verify a projected 3D translation gizmo actually draws in the viewport.

All captures use identical entity selection and PBR state; only the gizmo
visibility differs, so this isolates the axis-handle draw calls.
"""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as source:
        if source.readline() != b"P6\n":
            raise AssertionError("Invalid PPM signature")
        width, height = map(int, source.readline().split())
        if source.readline() != b"255\n":
            raise AssertionError("Invalid PPM bit depth")
        pixels = source.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError("Truncated GUI screenshot")
    return (width, height), pixels


def run(xvfb, engine, directory, gizmo):
    path = directory / ("v16-gizmo-on.ppm" if gizmo else "v16-gizmo-off.ppm")
    cmd = [xvfb, "-a", engine, "--gui", "1", "--gui-select", "13",
           "--gizmo", "1" if gizmo else "0",
           "--frames", "2", "--sim-dt", "0", "--lights", "0",
           "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
           "--sun-cascades", "0", "--point-shadows", "0",
           "--bloom", "0", "--stats", "--screenshot", str(path)]
    result = subprocess.run(cmd, check=True, capture_output=True,
                            text=True, timeout=120)
    for name, expected in [("gizmoEnabled", int(gizmo)),
                           ("guiVisible", 1), ("sceneEntities", 26),
                           ("sceneRenderables", 26),
                           ("editorUndoCount", 0), ("editorRedoCount", 0)]:
        match = re.search(r"\b" + name + r"=(\d+)", result.stdout)
        if not match or int(match.group(1)) != expected:
            raise AssertionError(
                f"{name}: expected {expected}, got: {result.stdout}")
    return ppm(path)


def main():
    xvfb, engine, target = sys.argv[1:]
    directory = Path(target)
    directory.mkdir(parents=True, exist_ok=True)
    no_gizmo = run(xvfb, engine, directory, False)
    with_gizmo = run(xvfb, engine, directory, True)
    if no_gizmo[0] != with_gizmo[0]:
        raise AssertionError("Gizmo captures have different sizes")

    differences = [abs(a-b) for a,b in zip(no_gizmo[1], with_gizmo[1])]
    changed = sum(v >= 8 for v in differences)
    mean = sum(differences) / len(differences)
    print(f"3D gizmo on/off: mean RGB delta {mean:.5f}, "
          f"channels changed >=8: {changed}/{len(differences)}")
    if mean < 0.008 or changed < 100:
        raise AssertionError("Gizmo axes did not visibly appear in viewport")
    print("3D axis gizmo overlay and editor diagnostic counters verified")


if __name__ == "__main__":
    main()

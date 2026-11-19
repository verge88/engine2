#!/usr/bin/env python3
"""Headless AABB/sphere demo, material rendering, physics, and JSON roundtrip."""
import json
from pathlib import Path
import re
import subprocess
import sys


def capture_ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Invalid P6 screenshot: {path}")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Wrong pixel format")
        pixels = stream.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError("Bad PPM dimensions")
    return (width, height), pixels


def engine(xvfb, binary, *extra):
    cmd = [xvfb, "-a", binary, "--lights", "0", "--point-shadows", "0",
           "--sun-cascades", "0", "--ssao", "0", "--taa", "0",
           "--auto-exposure", "0", "--bloom", "0",
           "--stats", *[str(e) for e in extra]]
    result = subprocess.run(cmd, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise AssertionError(f"engine2 failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def stat(stdout, key):
    match = re.search(r"\b" + re.escape(key) + r"=([-\d.]+)", stdout)
    if not match:
        raise AssertionError(f"Missing {key} in engine stats: {stdout}")
    return float(match.group(1))


def compare(a, b, expect_same):
    size_a, rgb_a = capture_ppm(a)
    size_b, rgb_b = capture_ppm(b)
    if size_a != size_b:
        raise AssertionError("Image size differs")
    differences = [abs(x-y) for x, y in zip(rgb_a, rgb_b)]
    mean = sum(differences) / len(differences)
    changed = sum(d >= 5 for d in differences)
    print(f"{a.name} vs {b.name}: mean channel delta {mean:.4f}, "
          f"significant channels={changed}/{len(differences)}")
    if expect_same:
        if mean > 0.08 or changed > len(differences)//2000:
            raise AssertionError("Saved AABB scene rendered differently after reload")
    elif mean < 0.05 or changed < len(differences)//5000:
        raise AssertionError("Box demo did not create visible geometry")


def main():
    xvfb, binary, directory = sys.argv[1:]
    path = Path(directory)
    path.mkdir(parents=True, exist_ok=True)
    baseline = path / "v13-baseline.ppm"
    before = path / "v13-contact.ppm"
    after = path / "v13-reloaded.ppm"
    scene = path / "v13-box-scene.json"

    engine(xvfb, binary, "--frames", 1, "--sim-dt", 0,
           "--screenshot", baseline)
    output = engine(xvfb, binary, "--box-demo", 1, "--frames", 12,
                    "--sim-dt", 0.05, "--screenshot", before,
                    "--save-scene", scene)
    if stat(output, "sceneBodies") != 2 or stat(output, "sceneEntities") != 28:
        raise AssertionError("Expected two colliders plus 26 procedural entities")
    if stat(output, "sceneBoxContacts") < 1 or stat(output, "sceneBroadphasePairs") < 1:
        raise AssertionError("Sphere-box contact did not resolve")
    if stat(output, "sceneSphereContacts") != 0:
        raise AssertionError("Unexpected sphere-sphere contacts")
    if stat(output, "boxSphereVelocityX") >= -2.0:
        raise AssertionError("Sphere failed to bounce off the box")
    if abs(stat(output, "boxObstacleX")) > 0.0001:
        raise AssertionError("Static box moved during impact")
    if stat(output, "boxSphereX") > -0.94:
        raise AssertionError("Dynamic sphere is inside AABB")
    compare(baseline, before, expect_same=False)

    snapshot = json.loads(scene.read_text())
    bodies = [e["rigidBody"] for e in snapshot["entities"] if "rigidBody" in e]
    if sorted(b["shape"] for b in bodies) != ["BOX", "SPHERE"]:
        raise AssertionError("Persisted JSON lost collider shape")
    if not any(b["shape"] == "BOX" and b["halfExtents"] == [0.5]*3
               and b["isStatic"] for b in bodies):
        raise AssertionError("Box half-extents/static flag not persisted")
    restore = engine(xvfb, binary, "--load-scene", scene, "--frames", 1,
                     "--sim-dt", 0, "--screenshot", after)
    if stat(restore, "sceneBodies") != 2 or stat(restore, "sceneFixedSteps") != 0:
        raise AssertionError("Loaded scene advanced physics unexpectedly")
    compare(before, after, expect_same=True)
    print("AABB physics and JSON roundtrip verified")


if __name__ == "__main__":
    main()

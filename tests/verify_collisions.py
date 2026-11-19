#!/usr/bin/env python3
"""Regression: colliding sphere entities must generate contacts and visible pixels."""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path: Path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Not a P6 screenshot: {path}")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Expected 8-bit RGB screenshot")
        pixels = stream.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError("Screenshot has invalid dimensions")
    return (width, height), pixels


def run(xvfb: str, binary: str, directory: Path, collisions: bool):
    name = "colliding" if collisions else "baseline"
    image = directory / f"v11-{name}.ppm"
    args = [
        xvfb, "-a", binary,
        "--collision-demo", "1" if collisions else "0",
        "--sim-dt", "0.05", "--frames", "12",
        "--lights", "0", "--point-shadows", "0", "--sun-cascades", "0",
        "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
        "--bloom", "0", "--stats", "--screenshot", str(image),
    ]
    result = subprocess.run(args, check=True, capture_output=True,
                            text=True, timeout=120)

    def value(key):
        match = re.search(r"\b" + re.escape(key) + r"=([-\d.]+)",
                          result.stdout)
        if not match:
            raise AssertionError(f"Missing {key} in stats:\n{result.stdout}")
        return float(match.group(1))

    expected_entities = 28 if collisions else 26
    if value("sceneEntities") != expected_entities:
        raise AssertionError(f"{name}: unexpected entity count")
    if value("sceneRenderables") != expected_entities:
        raise AssertionError(f"{name}: expected one mesh per entity")
    if value("sceneBodies") != (2 if collisions else 0):
        raise AssertionError(f"{name}: unexpected rigid body count")
    if value("sceneFixedSteps") < 32:
        raise AssertionError(f"{name}: fixed-step loop did not update")

    if collisions:
        pairs = value("sceneBroadphasePairs")
        contacts = value("sceneSphereContacts")
        separation = value("collisionSeparation")
        a = value("collisionVelocityA")
        b = value("collisionVelocityB")
        print(f"Collision runtime: pairs={pairs}, contacts={contacts}, "
              f"separation={separation:.4f}, velocities={a:.3f}/{b:.3f}")
        if pairs < 1 or contacts < 1:
            raise AssertionError("No sphere collisions detected")
        if separation < 1.099:
            raise AssertionError("Colliders remain interpenetrating")
        if a >= -0.1 or b <= 0.1:
            raise AssertionError("Collision impulse did not reverse sphere velocities")
    return ppm(image)


def main():
    xvfb, binary, output = sys.argv[1:]
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=True)
    baseline = run(xvfb, binary, directory, False)
    collision = run(xvfb, binary, directory, True)
    if baseline[0] != collision[0]:
        raise AssertionError("Mismatched screenshots")
    left, right = baseline[1], collision[1]
    diffs = [abs(a - b) for a, b in zip(left, right)]
    mean = sum(diffs) / len(diffs)
    changed = sum(delta >= 5 for delta in diffs)
    print(f"Sphere collision screenshot mean RGB delta={mean:.3f}, "
          f"significant channels={changed}/{len(diffs)}")
    if mean < 0.05 or changed < len(diffs) // 5000:
        raise AssertionError("The collision scene did not change visible pixels")


if __name__ == "__main__":
    main()

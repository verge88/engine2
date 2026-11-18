#!/usr/bin/env python3
"""Run the same procedural scene with and without a simulated rigid body."""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as f:
        if f.readline() != b"P6\n":
            raise AssertionError(f"Not a P6 screenshot: {path}")
        width, height = map(int, f.readline().split())
        if f.readline() != b"255\n":
            raise AssertionError("Screenshot must use 8-bit RGB channels")
        rgb = f.read()
    if width < 128 or height < 128 or len(rgb) != width * height * 3:
        raise AssertionError("Invalid screenshot payload")
    return (width, height), rgb


def capture(xvfb, executable, directory, physics):
    name = "falling" if physics else "static"
    output = directory / f"v10-{name}.ppm"
    cmd = [
        xvfb, "-a", executable, "--frames", "12",
        "--physics-demo", str(int(physics)), "--sim-dt", "0.10",
        "--lights", "0", "--point-shadows", "0", "--sun-cascades", "0",
        "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
        "--bloom", "0", "--stats", "--screenshot", str(output),
    ]
    result = subprocess.run(
        cmd, capture_output=True, text=True, check=True, timeout=120
    )
    def stat(name):
        m = re.search(r"\b" + name + r"=([\d.-]+)", result.stdout)
        if not m:
            raise AssertionError(f"Missing SceneStats {name}:\n{result.stdout}")
        return float(m.group(1))

    expected_entities = 27 if physics else 26
    if stat("sceneEntities") != expected_entities:
        raise AssertionError(f"{name}: entity count does not match")
    if stat("sceneBodies") != int(physics):
        raise AssertionError(f"{name}: rigid-body count does not match")
    if stat("sceneRenderables") != expected_entities:
        raise AssertionError(f"{name}: renderable component count does not match")
    if stat("sceneFixedSteps") < 65:
        raise AssertionError(f"{name}: fixed physics updates did not occur")
    if physics:
        height = stat("physicsY")
        if height < -0.5001 or height > 3.0:
            raise AssertionError(f"Falling sphere did not move/collide correctly: {height}")
        print(f"Physics demo after 12 frames: sphere Y={height:.3f}")
    return ppm(output)


def main():
    xvfb, engine, output_dir = sys.argv[1:]
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=True)
    static = capture(xvfb, engine, directory, False)
    dynamic = capture(xvfb, engine, directory, True)
    if static[0] != dynamic[0]:
        raise AssertionError("Frame sizes differ")
    data_a, data_b = static[1], dynamic[1]
    mean = sum(abs(a - b) for a,b in zip(data_a, data_b)) / len(data_a)
    significant = sum(abs(a - b) >= 5 for a,b in zip(data_a, data_b))
    print(f"Scene physics image mean RGB change={mean:.3f}; "
          f"channels changed >=5: {significant}/{len(data_a)}")
    if mean < 0.04 or significant < len(data_a) // 5000:
        raise AssertionError("New entity/component did not alter rendered frame")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Exercise scene editor JSON patch, inspection, rendering and persisted assets."""
import json
from pathlib import Path
import re
import subprocess
import sys


def image(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Invalid screenshot signature: {path}")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Expected 8-bit PPM")
        data = stream.read()
    if width < 128 or height < 128 or len(data) != width * height * 3:
        raise AssertionError("Invalid screenshot dimensions")
    return (width, height), data


def run(xvfb, binary, *args, expect_failure=False):
    command = [
        xvfb, "-a", binary,
        "--frames", "1", "--sim-dt", "0",
        "--lights", "0", "--sun-cascades", "0", "--point-shadows", "0",
        "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
        "--bloom", "0", "--stats",
        *[str(arg) for arg in args],
    ]
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if expect_failure:
        if result.returncode == 0:
            raise AssertionError("Invalid scene edits unexpectedly succeeded")
    elif result.returncode != 0:
        raise AssertionError(f"engine2 failed: {result.stdout}\n{result.stderr}")
    return result.stdout


def statistic(output, name):
    match = re.search(r"\b" + re.escape(name) + r"=([\d.]+)", output)
    if not match:
        raise AssertionError(f"Missing {name}: {output}")
    return float(match.group(1))


def compare(a, b, expect_same):
    shape_a, pixels_a = image(a)
    shape_b, pixels_b = image(b)
    if shape_a != shape_b:
        raise AssertionError("Image dimensions changed")
    differences = [abs(x - y) for x, y in zip(pixels_a, pixels_b)]
    mean = sum(differences) / len(differences)
    changed = sum(delta >= 5 for delta in differences)
    print(f"{a.name} vs {b.name}: mean RGB delta={mean:.4f}, "
          f"channels changed>=5={changed}/{len(differences)}")
    if expect_same:
        if mean > 0.08 or changed > len(differences) // 2000:
            raise AssertionError("Saved edited scene did not reproduce rendered pixels")
    elif mean < 0.05 or changed < len(differences) // 5000:
        raise AssertionError("Scene editor did not visibly change the frame")


def main():
    xvfb, binary, output_dir, fixture = sys.argv[1:]
    dest = Path(output_dir)
    dest.mkdir(parents=True, exist_ok=True)
    baseline_frame = dest / "v14-original.ppm"
    edited_frame = dest / "v14-edited.ppm"
    reloaded_frame = dest / "v14-reloaded.ppm"
    baseline_inspect = dest / "v14-inspect-original.json"
    edited_inspect = dest / "v14-inspect-edited.json"
    scene_path = dest / "v14-edited-scene.json"

    start = run(xvfb, binary, "--inspect-scene", baseline_inspect,
                "--screenshot", baseline_frame)
    initial = json.loads(baseline_inspect.read_text())
    if initial["format"] != "engine2.scene-inspection" or initial["version"] != 1:
        raise AssertionError("Invalid scene inspection JSON format/version")
    if initial["summary"]["entities"] != 26:
        raise AssertionError("Inspector omitted procedural scene entities")
    if initial["entities"][13]["name"] != "Mesh 13":
        raise AssertionError("Inspector index is inconsistent with scene JSON")
    if initial["entities"][13]["renderer"]["mesh"] != "procedural/sphere":
        raise AssertionError("Inspector lost stable renderer asset key")
    if statistic(start, "sceneEntities") != 26:
        raise AssertionError("Procedural scene has wrong entity count")

    changed = run(xvfb, binary, "--edit-scene", fixture,
                  "--inspect-scene", edited_inspect,
                  "--save-scene", scene_path, "--screenshot", edited_frame)
    if "Scene edits applied: 4" not in changed:
        raise AssertionError("JSON batch edit was not applied")
    if statistic(changed, "submitted") != 25:
        raise AssertionError("Disabled sphere should not submit a mesh")
    edited = json.loads(edited_inspect.read_text())
    moved = edited["entities"][13]
    pos = moved["worldPosition"]
    if moved["name"] != "Floating green sphere" or any(
        abs(a - b) > 0.0002 for a, b in zip(pos, [0.0, 1.4, 0.0])
    ):
        raise AssertionError("Inspector didn't preserve edited entity world position")
    if moved["renderer"]["albedo"][1] < 0.94:
        raise AssertionError("Mesh renderer albedo edit did not apply")
    if edited["entities"][14]["enabled"] or edited["entities"][14]["effectiveEnabled"]:
        raise AssertionError("Disabled editor entity still appears enabled")
    compare(baseline_frame, edited_frame, expect_same=False)

    persisted = json.loads(scene_path.read_text())
    if persisted["entities"][13]["name"] != moved["name"]:
        raise AssertionError("Scene JSON didn't persist the new name")
    if persisted["entities"][14]["enabled"]:
        raise AssertionError("Scene JSON didn't persist disabled visibility")

    result = run(xvfb, binary, "--load-scene", scene_path,
                 "--screenshot", reloaded_frame)
    if statistic(result, "submitted") != 25:
        raise AssertionError("Saved scene restored wrong visible mesh count")
    compare(edited_frame, reloaded_frame, expect_same=True)

    invalid = dest / "v14-invalid-operations.json"
    invalid.write_text(json.dumps({
        "format": "engine2.scene-edits", "version": 1,
        "operations": [
            {"op": "rename", "entity": 13, "name": "Should not commit"},
            {"op": "set-position", "entity": 999, "position": [0, 0, 0]}
        ],
    }))
    run(xvfb, binary, "--load-scene", scene_path,
        "--edit-scene", invalid, expect_failure=True)
    print("Scene inspector JSON report, transactional editor, visual changes "
          "and edited-scene reload verified")


if __name__ == "__main__":
    main()

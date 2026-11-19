#!/usr/bin/env python3
"""End-to-end scene JSON save/reload of physics and textured glTF materials."""
import json
from pathlib import Path
import re
import subprocess
import sys


def ppm(path: Path):
    with path.open("rb") as source:
        if source.readline() != b"P6\n":
            raise AssertionError(f"Invalid screenshot signature: {path}")
        width, height = (int(v) for v in source.readline().split())
        if source.readline() != b"255\n":
            raise AssertionError(f"Expected 8-bit RGB: {path}")
        rgb = source.read()
    if width < 128 or height < 128 or len(rgb) != width * height * 3:
        raise AssertionError(f"Invalid rendered image: {path}")
    return (width, height), rgb


def compare_images(first: Path, second: Path, label: str):
    shape1, a = ppm(first)
    shape2, b = ppm(second)
    if shape1 != shape2:
        raise AssertionError(f"{label}: screenshot dimensions differ")
    differences = [abs(x - y) for x, y in zip(a, b)]
    mean = sum(differences) / len(differences)
    changed = sum(diff >= 4 for diff in differences)
    print(f"{label} roundtrip: mean channel delta={mean:.5f}, "
          f"channels changed >=4: {changed}/{len(differences)}")
    # Resolved PBR assets and transforms should reproduce *nearly identical*
    # pixels. Allow tiny driver variance but reject broken/missing resources.
    if mean > 0.08 or changed > len(differences) // 2000:
        raise AssertionError(f"{label}: visible scene was not restored accurately")


def execute(xvfb, engine, *extra, fail=False):
    args = [
        xvfb, "-a", engine, "--lights", "0",
        "--sun-cascades", "0", "--point-shadows", "0",
        "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
        "--bloom", "0", "--stats", *[str(value) for value in extra],
    ]
    result = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if fail:
        if result.returncode == 0:
            raise AssertionError("Malformed scene unexpectedly loaded")
        print(f"Invalid input rejected: {result.stderr.strip()[:240]}")
    elif result.returncode != 0:
        raise AssertionError(f"Scene run failed: {result.stderr}\n{result.stdout}")
    return result.stdout


def stat(output, key):
    match = re.search(r"\b" + re.escape(key) + r"=([-\d.]+)", output)
    if not match:
        raise AssertionError(f"Missing stats {key}: {output}")
    return float(match.group(1))


def main():
    xvfb, engine, output, gltf = sys.argv[1:]
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=True)

    # A fully simulated state is captured at frame 12, then restored without
    # advancing physics. This checks entity components, mesh keys, transforms,
    # impulse velocities, and the rendered image.
    state = directory / "v12-physics-scene.json"
    second_state = directory / "v12-physics-scene-reloaded.json"
    before = directory / "v12-physics-before.ppm"
    after = directory / "v12-physics-after.ppm"
    first = execute(
        xvfb, engine, "--collision-demo", 1,
        "--sim-dt", 0.05, "--frames", 12,
        "--save-scene", state, "--screenshot", before
    )
    if stat(first, "sceneBodies") != 2 or stat(first, "sceneFixedSteps") < 30:
        raise AssertionError("Collision snapshot did not contain active physics")

    snapshot = json.loads(state.read_text())
    if snapshot.get("format") != "engine2.scene" or snapshot.get("version") != 1:
        raise AssertionError("Scene JSON header/version is invalid")
    if len(snapshot["entities"]) != 28:
        raise AssertionError("Scene JSON lost or duplicated procedural entities")
    bodies = [e["rigidBody"] for e in snapshot["entities"] if "rigidBody" in e]
    if len(bodies) != 2 or not all(body["mass"] > 0 for body in bodies):
        raise AssertionError("Rigidbody data was not serialized")

    restored = execute(
        xvfb, engine, "--load-scene", state, "--sim-dt", 0,
        "--frames", 1, "--save-scene", second_state, "--screenshot", after
    )
    if stat(restored, "sceneEntities") != 28 or stat(restored, "sceneBodies") != 2:
        raise AssertionError("Deserialized scene component counts differ")
    if stat(restored, "sceneFixedSteps") != 0:
        raise AssertionError("Physics was advanced despite zero simulation delta")
    if json.loads(second_state.read_text()) != snapshot:
        raise AssertionError("Saved Scene JSON changed after a load/save cycle")
    compare_images(before, after, "Physics components")

    # A glTF fixture with embedded RGBA textures / MASK alpha exercises GPU
    # texture ID remapping through stable gltf/texture/N registry keys.
    gltf_scene = directory / "v12-gltf-scene.json"
    gltf_before = directory / "v12-gltf-before.ppm"
    gltf_after = directory / "v12-gltf-after.ppm"
    execute(
        xvfb, engine, "--model", gltf,
        "--sim-dt", 0, "--frames", 1,
        "--save-scene", gltf_scene, "--screenshot", gltf_before
    )
    loaded_gltf = json.loads(gltf_scene.read_text())
    materials = [e["renderer"]["material"] for e in loaded_gltf["entities"]
                 if "renderer" in e]
    if not any(m["alphaMode"] == "MASK" and
               isinstance(m["textures"]["baseColor"], str) for m in materials):
        raise AssertionError("glTF MASK material / RGBA texture key not saved")
    execute(
        xvfb, engine, "--model", gltf, "--load-scene", gltf_scene,
        "--sim-dt", 0, "--frames", 1, "--screenshot", gltf_after
    )
    compare_images(gltf_before, gltf_after, "Textured glTF MASK")

    # Missing asset refs must never silently substitute an unrelated mesh.
    execute(xvfb, engine, "--load-scene", gltf_scene,
            "--sim-dt", 0, "--frames", 1, fail=True)
    incompatible = directory / "v12-invalid-version.json"
    broken = dict(snapshot)
    broken["version"] = 99
    incompatible.write_text(json.dumps(broken))
    execute(xvfb, engine, "--load-scene", incompatible,
            "--frames", 1, fail=True)

    print("Scene JSON physics/glTF roundtrip and validation passed")


if __name__ == "__main__":
    main()

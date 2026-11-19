#!/usr/bin/env python3
"""Headless end-to-end Dear ImGui overlay, selected-entity and edit rendering.

The previous rendering regressions use --frames, so GUI is hidden by default.
This test explicitly enables it and compares the *captured default framebuffer*
against a GUI-free baseline to verify actual GPU GUI composition.
"""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Invalid P6 screenshot: {path}")
        size = tuple(map(int, stream.readline().split()))
        if stream.readline() != b"255\n":
            raise AssertionError("Expected 8-bit RGB image")
        pixels = stream.read()
    if len(size) != 2 or size[0] < 128 or size[1] < 128:
        raise AssertionError("Invalid frame dimensions")
    if len(pixels) != size[0] * size[1] * 3:
        raise AssertionError("Image data truncated")
    return size, pixels


def capture(xvfb, binary, output, *, gui, selection=None, edits=None,
            expected_meshes=26):
    path = output / (
        "v15-" + ("hidden" if not gui else
                   f"selected-{selection}" + ("-edited" if edits else "")) + ".ppm")
    args = [
        xvfb, "-a", binary,
        "--gui", "1" if gui else "0",
        "--lights", "0", "--frames", "2", "--sim-dt", "0",
        "--sun-cascades", "0", "--point-shadows", "0",
        "--taa", "0", "--ssao", "0",
        "--auto-exposure", "0", "--bloom", "0",
        "--stats", "--screenshot", str(path),
    ]
    if selection is not None:
        args += ["--gui-select", str(selection)]
    if edits:
        args += ["--edit-scene", str(edits)]
    result = subprocess.run(args, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise AssertionError(f"Engine GUI run failed: {result.stdout}\n{result.stderr}")
    matches = dict(re.findall(r"(guiVisible|sceneEntities|submitted|sceneRenderables)=([0-9]+)",
                              result.stdout))
    if int(matches.get("guiVisible", -1)) != int(gui):
        raise AssertionError(f"Incorrect GUI enabled mode: {result.stdout}")
    if int(matches.get("sceneEntities", -1)) != 26 or (
        int(matches.get("sceneRenderables", -1)) != 26 or
        int(matches.get("submitted", -1)) != expected_meshes
    ):
        raise AssertionError(f"Scene components missing from GUI render: {result.stdout}")
    if edits and "Scene edits applied: 4" not in result.stdout:
        raise AssertionError("JSON changes not applied to GUI scene")
    return ppm(path)


def compare(first, second, name, minimum_mean, minimum_changed):
    if first[0] != second[0]:
        raise AssertionError("Compared frames have different sizes")
    differences = [abs(a-b) for a, b in zip(first[1], second[1])]
    mean = sum(differences) / len(differences)
    changed = sum(delta >= 5 for delta in differences)
    print(f"{name}: mean RGB delta={mean:.4f}, channels changed>=5={changed}")
    if mean < minimum_mean or changed < minimum_changed:
        raise AssertionError(f"{name} produced no meaningful visible difference")


def main():
    xvfb, binary, outdir, edits = sys.argv[1:]
    output = Path(outdir)
    output.mkdir(parents=True, exist_ok=True)
    hidden = capture(xvfb, binary, output, gui=False)
    ground = capture(xvfb, binary, output, gui=True, selection=0)
    sphere = capture(xvfb, binary, output, gui=True, selection=13)
    modified = capture(xvfb, binary, output, gui=True, selection=13,
                       edits=edits, expected_meshes=25)
    compare(hidden, ground, "Dear ImGui overlay on/off",
            minimum_mean=0.3, minimum_changed=3000)
    compare(ground, sphere, "Selected entity properties",
            minimum_mean=0.001, minimum_changed=25)
    compare(sphere, modified, "Live edited PBR scene and inspector",
            minimum_mean=0.02, minimum_changed=200)

    bad = subprocess.run([xvfb, "-a", binary, "--frames", "1",
                          "--gui", "1", "--gui-select", "999999"],
                         text=True, capture_output=True, timeout=120)
    if bad.returncode == 0 or "out of range" not in bad.stderr:
        raise AssertionError("Invalid GUI entity selection was not rejected")
    print("ImGui overlay, selection, edited scene and validation verified")


if __name__ == "__main__":
    main()

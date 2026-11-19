#!/usr/bin/env python3
"""OpenGL/JSON regression: v2.1 contact friction, editor patches and persistence."""
import json
from pathlib import Path
import re
import subprocess
import sys


def run(xvfb, binary, *args, frames=1, dt=0.0, failure=False):
    cmd = [xvfb, "-a", binary,
           "--frames", str(frames), "--sim-dt", str(dt),
           "--lights", "0", "--gui", "0",
           "--sun-cascades", "0", "--point-shadows", "0",
           "--taa", "0", "--ssao", "0", "--auto-exposure", "0",
           "--bloom", "0", "--stats", *map(str, args)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if failure:
        if result.returncode == 0:
            raise AssertionError("Invalid friction parameters unexpectedly accepted")
    elif result.returncode:
        raise AssertionError(f"engine2 failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def ppm(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError("Expected P6 screenshot")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Expected 8-bit RGB screenshot")
        pixels = stream.read()
    if width < 128 or height < 128 or len(pixels) != width*height*3:
        raise AssertionError("Incomplete frame")
    return (width,height), pixels


def compare(a, b, must_differ):
    size_a, pix_a = ppm(a)
    size_b, pix_b = ppm(b)
    if size_a != size_b:
        raise AssertionError("Incompatible screenshot dimensions")
    values = [abs(x-y) for x,y in zip(pix_a,pix_b)]
    mean = sum(values)/len(values)
    changed = sum(d >= 5 for d in values)
    print(f"{a.name} vs {b.name}: RGB mean delta={mean:.5f}, changed={changed}")
    if must_differ and (mean < 0.012 or changed < 125):
        raise AssertionError("Dynamic contact friction did not change rendered scene")
    if not must_differ and (mean > 0.08 or changed > len(values)//2000):
        raise AssertionError("Saving and reloading the friction scene changed its image")


def stat(output,key):
    match = re.search(r"\b"+re.escape(key)+r"=(-?\d+)",output)
    if not match:
        raise AssertionError(f"Missing {key} statistic: {output}")
    return int(match.group(1))


def write_patch(path, operations):
    path.write_text(json.dumps({
        "format":"engine2.scene-edits",
        "version":1,
        "operations":operations,
    },indent=2),encoding="utf-8")


def main():
    xvfb, binary, output = sys.argv[1:]
    directory = Path(output)
    directory.mkdir(parents=True,exist_ok=True)
    initial=directory/"v21-initial-scene.json"
    original=run(xvfb,binary,"--box-demo","1","--save-scene",initial)
    scene=json.loads(initial.read_text(encoding="utf-8"))
    items=scene["entities"]
    box=next(i for i,node in enumerate(items) if node["name"]=="Box obstacle")
    ball=next(i for i,node in enumerate(items) if node["name"]=="Sphere projectile")
    if box==ball or len(items)<28:
        raise AssertionError("Box collision procedural demo not available")

    velocities=[{"op":"set-body-velocity","entity":ball,
                 "velocity":[3.2,0,1.1]}]
    friction=[
        {"op":"set-body-friction","entity":box,
         "staticFriction":2.0,"dynamicFriction":1.5},
        {"op":"set-body-friction","entity":ball,
         "staticFriction":2.0,"dynamicFriction":1.5},
    ]
    basic_patch=directory/"v21-frictionless.json"
    friction_patch=directory/"v21-with-friction.json"
    write_patch(basic_patch,velocities)
    write_patch(friction_patch,velocities+friction)

    baseline=directory/"v21-frictionless.ppm"
    changed=directory/"v21-friction.ppm"
    persisted=directory/"v21-friction-saved.json"
    status_a=run(xvfb,binary,"--load-scene",initial,
                 "--edit-scene",basic_patch,"--screenshot",baseline,
                 frames=6,dt=0.1)
    status_b=run(xvfb,binary,"--load-scene",initial,
                 "--edit-scene",friction_patch,"--screenshot",changed,
                 "--save-scene",persisted,
                 frames=6,dt=0.1)
    for status in (status_a,status_b):
        if stat(status,"sceneBodies")!=2 or stat(status,"sceneBoxContacts")<1:
            raise AssertionError("Sphere/box contact was not resolved")
        if stat(status,"sceneSolverPasses")!=8*stat(status,"sceneFixedSteps"):
            raise AssertionError("Sequential solver must execute eight passes per fixed step")
    compare(baseline,changed,True)

    saved=json.loads(persisted.read_text(encoding="utf-8"))
    saved_box=saved["entities"][box]["rigidBody"]
    saved_ball=saved["entities"][ball]["rigidBody"]
    for body in (saved_box,saved_ball):
        if abs(body["staticFriction"]-2.0)>1.e-6 or (
            abs(body["dynamicFriction"]-1.5)>1.e-6
        ):
            raise AssertionError("SceneIO did not persist contact friction")

    restored=directory/"v21-friction-restored.ppm"
    run(xvfb,binary,"--load-scene",persisted,"--screenshot",restored,
        frames=1,dt=0.0)
    compare(changed,restored,False)

    bad=directory/"v21-bad-friction.json"
    write_patch(bad,[{"op":"set-body-friction","entity":ball,
                      "staticFriction":0.1,"dynamicFriction":2.0}])
    run(xvfb,binary,"--load-scene",initial,"--edit-scene",bad,
        frames=1,dt=0.0,failure=True)
    print("Contact friction visual response, eight-pass solver, JSON roundtrip and validation passed")


if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""v2.4 real-OpenGL regression for exact face/edge/corner sphere-box CCD.

Builds reproducible JSON scenes from the engine's box demo, then verifies
CCD counters, impulse directions, saved Scene state and actual PPM frames.
"""
import copy
import json
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as f:
        if f.readline()!=b"P6\n":
            raise AssertionError("Expected RGB PPM screenshot")
        width,height=map(int,f.readline().split())
        if f.readline()!=b"255\n":
            raise AssertionError("Expected 8-bit screenshot")
        pixels=f.read()
    if width<128 or height<128 or len(pixels)!=width*height*3:
        raise AssertionError("Incomplete OpenGL screenshot")
    return (width,height),pixels


def compare(first,second,title,visible):
    (size_a,a),(size_b,b)=ppm(first),ppm(second)
    if size_a!=size_b:
        raise AssertionError(f"{title}: image dimensions changed")
    values=[abs(x-y) for x,y in zip(a,b)]
    mean=sum(values)/len(values)
    changed=sum(d>=7 for d in values)
    print(f"{title}: mean RGB delta={mean:.5f}, changed channels={changed}")
    if visible and (mean<0.01 or changed<160):
        raise AssertionError(f"{title}: CCD deflection not visible")
    if not visible and (mean>0.03 or changed>len(values)//3000):
        raise AssertionError(f"{title}: expected identical rendered scene")


def run(xvfb,engine,*args,dt=0.0166667):
    command=[
        xvfb,"-a",engine,"--frames","1","--sim-dt",str(dt),
        "--lights","0","--gui","0","--stats",
        "--sun-cascades","0","--point-shadows","0",
        "--ssao","0","--taa","0","--auto-exposure","0",
        "--bloom","0",*map(str,args)
    ]
    process=subprocess.run(command,capture_output=True,text=True,timeout=120)
    if process.returncode:
        raise AssertionError(f"engine2 failed:\n{process.stdout}\n{process.stderr}")
    def count(key):
        match=re.search(r"\b"+re.escape(key)+r"=(-?\d+)",process.stdout)
        if not match:
            raise AssertionError(f"Missing {key} in {process.stdout}")
        return int(match.group(1))
    return count


def prepare_scene(baseline,y,z,continuous):
    scene=copy.deepcopy(baseline)
    body=next(node for node in scene["entities"]
              if node["name"]=="Sphere projectile")
    obstacle=next(node for node in scene["entities"]
                  if node["name"]=="Box obstacle")
    obstacle["rigidBody"]["halfExtents"]=[0.5,0.5,0.5]
    obstacle["rigidBody"]["restitution"]=1.0
    body["local"][12]=-2.0
    body["local"][13]=1.15+y
    body["local"][14]=1.5+z
    body["rigidBody"]["radius"]=0.3
    body["rigidBody"]["velocity"]=[240.0,0.0,0.0]
    body["rigidBody"]["restitution"]=1.0
    body["rigidBody"]["groundHeight"]=-100.0
    body["rigidBody"]["continuous"]=continuous
    return scene


def main():
    xvfb,engine,directory=sys.argv[1:]
    out=Path(directory)
    out.mkdir(exist_ok=True,parents=True)
    base=out/"v24-baseline.json"
    run(xvfb,engine,"--box-demo","1","--save-scene",base,dt=0)
    baseline=json.loads(base.read_text(encoding="utf-8"))
    cases={
        "face":(0.0,0.0,"sceneCcdBoxFaceHits"),
        "edge":(0.7,0.0,"sceneCcdBoxEdgeHits"),
        "corner":(0.7,0.7,"sceneCcdBoxCornerHits"),
        "false-corner":(0.79,0.79,None),
    }
    for name,(y,z,expected) in cases.items():
        file=out/f"v24-{name}.json"
        frame=out/f"v24-{name}.ppm"
        final=out/f"v24-{name}-saved.json"
        file.write_text(json.dumps(
            prepare_scene(baseline,y,z,True)),encoding="utf-8")
        stats=run(xvfb,engine,"--load-scene",file,
                  "--screenshot",frame,"--save-scene",final)
        if stats("sceneFixedSteps")!=1 or stats("sceneSolverPasses")!=8:
            raise AssertionError(f"{name}: fixed-step solver changed")
        if stats("sceneCcdDynamicHits")!=0 or stats("sceneCcdBudgetExhaustions")!=0:
            raise AssertionError(f"{name}: unexpected dynamic CCD or event limit")
        keys=("sceneCcdBoxFaceHits","sceneCcdBoxEdgeHits",
              "sceneCcdBoxCornerHits")
        for key in keys:
            if stats(key)!=(1 if key==expected else 0):
                raise AssertionError(f"{name}: misclassified {key}")
        if stats("sceneCcdHits")!=(0 if expected is None else 1):
            raise AssertionError(f"{name}: wrong CCD impact count")
        after=json.loads(final.read_text(encoding="utf-8"))
        sphere=next(node for node in after["entities"]
                    if node["name"]=="Sphere projectile")
        velocity=sphere["rigidBody"]["velocity"]
        if expected=="sceneCcdBoxCornerHits" and not (
            velocity[0]>0 and velocity[1]>100 and velocity[2]>100
        ):
            raise AssertionError("Rounded corner normal did not deflect sphere in 3D")
        if expected is None and abs(velocity[0]-240)>0.001:
            raise AssertionError("Expanded AABB corner incorrectly reversed velocity")

        # AABB false-positive path must render EXACTLY the same as discrete
        # physics; true face/edge/corner contacts must visibly change frame.
        discrete=out/f"v24-{name}-discrete.json"
        off=out/f"v24-{name}-discrete.ppm"
        discrete.write_text(json.dumps(
            prepare_scene(baseline,y,z,False)),encoding="utf-8")
        offStats=run(xvfb,engine,"--load-scene",discrete,"--screenshot",off)
        if offStats("sceneCcdHits")!=0:
            raise AssertionError("Discrete-only trajectory generated a CCD impact")
        compare(frame,off,name,visible=expected is not None)

        # Scene JSON roundtrip does not change the rendered state.
        if name=="corner":
            restored=out/"v24-corner-restored.ppm"
            run(xvfb,engine,"--load-scene",final,"--screenshot",restored,dt=0)
            compare(frame,restored,"corner state JSON roundtrip",visible=False)

    print("Exact face, edge and spherical corner CCD, false-positive "
          "rejection, OpenGL images and JSON roundtrip passed")


if __name__=="__main__":
    main()

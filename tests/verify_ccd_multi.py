#!/usr/bin/env python3
"""OpenGL regression for moving/moving CCD and multiple impacts in one step."""
import copy
import json
from pathlib import Path
import re
import subprocess
import sys


def image(path):
    with path.open("rb") as stream:
        if stream.readline()!=b"P6\n":
            raise AssertionError("Expected P6 screenshot")
        width,height=map(int,stream.readline().split())
        if stream.readline()!=b"255\n":
            raise AssertionError("Expected 8-bit RGB screenshot")
        pixels=stream.read()
    if width<128 or height<128 or len(pixels)!=width*height*3:
        raise AssertionError("Screenshot missing data")
    return (width,height),pixels


def difference(first,second,title):
    (shapeA,pixelsA),(shapeB,pixelsB)=image(first),image(second)
    if shapeA!=shapeB:
        raise AssertionError("OpenGL screenshot dimensions differ")
    delta=[abs(a-b) for a,b in zip(pixelsA,pixelsB)]
    mean=sum(delta)/len(delta)
    changed=sum(d>=7 for d in delta)
    print(f"{title}: mean RGB={mean:.5f}, channels>=7={changed}")
    if mean<0.01 or changed<170:
        raise AssertionError(f"{title}: CCD effect not visible in real frame")


def invoke(xvfb,exe,*args,dt=0.0166667,frames=1):
    result=subprocess.run([
        xvfb,"-a",exe,"--frames",str(frames),"--sim-dt",str(dt),
        "--lights","0","--sun-cascades","0","--point-shadows","0",
        "--taa","0","--ssao","0","--auto-exposure","0",
        "--bloom","0","--gui","0","--stats",
        *[str(value) for value in args]
    ],capture_output=True,text=True,timeout=120)
    if result.returncode:
        raise AssertionError(f"Execution failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def stat(log,name):
    match=re.search(r"\b"+re.escape(name)+r"=(-?[\d.]+)",log)
    if not match:
        raise AssertionError(f"Missing statistic {name}: {log}")
    return float(match.group(1))


def main():
    xvfb,exe,directory=sys.argv[1:]
    out=Path(directory)
    out.mkdir(exist_ok=True,parents=True)

    # Two high-speed spheres exchange their ordering without opt-in CCD.
    # Relative-motion TOI must prevent this and reverse both velocities.
    no=out/"v23-dynamic-discrete.ppm"
    yes=out/"v23-dynamic-ccd.ppm"
    plain=invoke(xvfb,exe,"--collision-demo","1",
                 "--ccd","0","--ccd-speed","180","--screenshot",no)
    precise=invoke(xvfb,exe,"--collision-demo","1",
                   "--ccd","1","--ccd-speed","180","--screenshot",yes)
    if stat(plain,"sceneCcdHits")!=0 or stat(plain,"collisionSeparation")>=0:
        raise AssertionError("Discrete head-on sphere crossing regression changed")
    if stat(precise,"sceneCcdHits")!=1 or (
        stat(precise,"sceneCcdDynamicHits")!=1
        or stat(precise,"sceneCcdBudgetExhaustions")!=0
        or stat(precise,"collisionSeparation")<=0
        or stat(precise,"collisionVelocityA")>=-150
        or stat(precise,"collisionVelocityB")<=150
    ):
        raise AssertionError(f"Dynamic/dynamic CCD failed: {precise}")
    difference(no,yes,"Two moving CCD spheres")

    # Export a real procedural glTF-compatible scene; duplicate the static
    # box into a two-wall corridor. With 240 world units/s the sphere must
    # bounce off BOTH static boxes in one 60 Hz fixed simulation step.
    source=out/"v23-corridor-base.json"
    invoke(xvfb,exe,"--box-demo","1","--save-scene",source,dt=0)
    world=json.loads(source.read_text(encoding="utf-8"))
    nodes=world["entities"]
    right=next(e for e in nodes if e["name"]=="Box obstacle")
    projectile=next(e for e in nodes if e["name"]=="Sphere projectile")
    left=copy.deepcopy(right)
    left["name"]="Left corridor wall"
    for obstacle,coordinate in ((right,1.5),(left,-1.5)):
        obstacle["local"][12]=coordinate
        obstacle["local"][0]=0.2
        obstacle["rigidBody"]["halfExtents"]=[0.1,0.5,0.5]
        obstacle["rigidBody"]["restitution"]=1.0
    projectile["local"][12]=0.0
    projectile["rigidBody"]["velocity"]=[240.0,0.0,0.0]
    projectile["rigidBody"]["restitution"]=1.0
    nodes.append(left)
    corridor=out/"v23-corridor.json"
    before=out/"v23-corridor-discrete.json"
    after=out/"v23-corridor-ccd.json"
    world_discrete=copy.deepcopy(world)
    # Identify the projectile by its stable exported name.
    for node in world_discrete["entities"]:
        if node["name"]=="Sphere projectile":
            node["rigidBody"]["continuous"]=False
    before.write_text(json.dumps(world_discrete),encoding="utf-8")
    for node in world["entities"]:
        if node["name"]=="Sphere projectile":
            node["rigidBody"]["continuous"]=True
    after.write_text(json.dumps(world),encoding="utf-8")

    img_old=out/"v23-corridor-no-ccd.ppm"
    img_new=out/"v23-corridor-two-hits.ppm"
    saved=out/"v23-corridor-saved.json"
    a=invoke(xvfb,exe,"--load-scene",before,"--screenshot",img_old)
    b=invoke(xvfb,exe,"--load-scene",after,
             "--screenshot",img_new,"--save-scene",saved)
    if stat(a,"sceneCcdHits")!=0:
        raise AssertionError("Disabled CCD unexpectedly registered a hit")
    if stat(b,"sceneCcdHits")!=2 or stat(b,"sceneCcdDynamicHits")!=0 or (
        stat(b,"sceneCcdBudgetExhaustions")!=0
    ):
        raise AssertionError(f"Expected exactly two CCD wall impacts: {b}")
    difference(img_old,img_new,"Two static-wall impacts in one step")

    loaded=json.loads(saved.read_text(encoding="utf-8"))
    saved_ball=next(n for n in loaded["entities"]
                    if n["name"]=="Sphere projectile")
    if saved_ball["rigidBody"]["continuous"] is not True:
        raise AssertionError("Continuous CCD flag lost in saved JSON")
    if saved_ball["rigidBody"]["velocity"][0]<230:
        raise AssertionError("Second CCD bounce did not reverse velocity again")
    if not -0.95<saved_ball["local"][12]<0.95:
        raise AssertionError("Projectile tunnelled outside CCD corridor")

    restored=out/"v23-corridor-restored.ppm"
    invoke(xvfb,exe,"--load-scene",saved,"--screenshot",restored,dt=0)
    size_a,a_pixels=image(img_new)
    size_b,b_pixels=image(restored)
    if size_a!=size_b:
        raise AssertionError("Saved CCD frame size changed")
    values=[abs(x-y) for x,y in zip(a_pixels,b_pixels)]
    if sum(values)/len(values)>0.1:
        raise AssertionError("Saving/reloading scene changed rendered CCD state")
    print("Moving sphere CCD, multiple wall hits, OpenGL output, and JSON persistency passed")


if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""Actual OpenGL smoke test for v2.2: sphere tunnelling vs opt-in CCD."""
from pathlib import Path
import json
import re
import subprocess
import sys


def screenshot(path):
    with path.open("rb") as f:
        if f.readline() != b"P6\n":
            raise AssertionError("Invalid screenshot file")
        width,height=map(int,f.readline().split())
        if f.readline() != b"255\n":
            raise AssertionError("Invalid image bit depth")
        pixels=f.read()
    if width<128 or height<128 or len(pixels)!=width*height*3:
        raise AssertionError("Incomplete image")
    return (width,height),pixels


def run(xvfb,exe,*args):
    command=[xvfb,"-a",exe,"--frames","1",
             "--sim-dt","0.0166667","--lights","0",
             "--sun-cascades","0","--point-shadows","0",
             "--ssao","0","--taa","0","--auto-exposure","0",
             "--bloom","0","--gui","0","--stats",*map(str,args)]
    result=subprocess.run(command,text=True,capture_output=True,timeout=120)
    if result.returncode:
        raise AssertionError(f"Run failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def number(text,key):
    match=re.search(r"\b"+re.escape(key)+r"=(-?[\d.]+)",text)
    if not match:
        raise AssertionError(f"Missing statistic {key}: {text}")
    return float(match.group(1))


def main():
    xvfb,exe,output=sys.argv[1:]
    dest=Path(output)
    dest.mkdir(parents=True,exist_ok=True)

    old=dest/"v22-no-ccd.ppm"
    new=dest/"v22-with-ccd.ppm"
    saved=dest/"v22-ccd-scene.json"
    a=run(xvfb,exe,"--box-demo","1","--ccd","0",
          "--ccd-speed","180","--screenshot",old)
    b=run(xvfb,exe,"--box-demo","1","--ccd","1",
          "--ccd-speed","180","--screenshot",new,
          "--save-scene",saved)

    if number(a,"sceneCcdHits")!=0 or number(a,"boxSphereX")<0.9:
        raise AssertionError("Expected high-speed sphere to tunnel WITHOUT CCD")
    if number(b,"sceneCcdHits")!=1 or number(b,"boxSphereX")>-0.95:
        raise AssertionError("Swept CCD failed to stop tunnelling through thin box")
    if number(b,"boxSphereVelocityX")>-100:
        raise AssertionError("CCD impulse did not reverse velocity")
    if number(b,"sceneBroadphaseCells")<1 or number(b,"sceneBroadphasePairs")<1:
        raise AssertionError("3D spatial broadphase telemetry not reported")
    if number(b,"sceneSolverPasses")!=8*number(b,"sceneFixedSteps"):
        raise AssertionError("Contact solver pass counts changed")

    size_a,pix_a=screenshot(old)
    size_b,pix_b=screenshot(new)
    if size_a!=size_b:
        raise AssertionError("CCD screenshots differ in dimensions")
    change=[abs(x-y) for x,y in zip(pix_a,pix_b)]
    mean=sum(change)/len(change)
    moved=sum(delta>=7 for delta in change)
    print(f"CCD off/on: mean RGB pixel delta={mean:.5f}, changed={moved}")
    if mean<0.01 or moved<160:
        raise AssertionError("CCD response did not visibly change OpenGL frame")

    state=json.loads(saved.read_text(encoding="utf-8"))
    projectile=next(node for node in state["entities"]
                    if node["name"]=="Sphere projectile")
    if projectile["rigidBody"]["continuous"] is not True:
        raise AssertionError("CCD body flag missing from serialized Scene JSON")
    loaded=dest/"v22-loaded.ppm"
    run(xvfb,exe,"--load-scene",saved,"--sim-dt","0",
        "--screenshot",loaded)
    size_c,pix_c=screenshot(loaded)
    if size_b!=size_c:
        raise AssertionError("Scene JSON roundtrip image dimensions changed")
    delta=[abs(x-y) for x,y in zip(pix_b,pix_c)]
    mean_reload=sum(delta)/len(delta)
    changed_reload=sum(v>=5 for v in delta)
    print(f"CCD scene reload: mean RGB delta={mean_reload:.5f}, "
          f"changed={changed_reload}")
    if mean_reload>0.08 or changed_reload>len(delta)//2000:
        raise AssertionError("CCD scene JSON roundtrip rendered differently")

    for options in [("--ccd","2"),("--ccd-speed","-10")]:
        result=subprocess.run([xvfb,"-a",exe,"--frames","1",
                               "--box-demo","1",*options],
                              text=True,capture_output=True,timeout=120)
        if result.returncode==0:
            raise AssertionError(f"Invalid CCD CLI accepted: {options}")
    print("3D broadphase + fast moving CCD sphere: screenshot and persistence passed")


if __name__=="__main__":
    main()

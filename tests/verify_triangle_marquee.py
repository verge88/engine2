#!/usr/bin/env python3
"""GPU-backed regression for v1.9 precise triangle and marquee picking."""
from pathlib import Path
import re
import subprocess
import sys


def parse_ppm(path):
    with path.open("rb") as file:
        assert file.readline()==b"P6\n", "PPM header missing"
        width,height=map(int,file.readline().split())
        assert file.readline()==b"255\n", "Invalid PPM bit depth"
        image=file.read()
    assert width>=128 and height>=128 and len(image)==width*height*3
    return (width,height),image


def difference(a,b,title,minimum_mean,minimum_channels):
    assert a[0]==b[0]
    d=[abs(x-y) for x,y in zip(a[1],b[1])]
    changed=sum(v>=8 for v in d)
    mean=sum(d)/len(d)
    print(f"{title}: mean={mean:.5f}, channels>=8={changed}")
    if changed<minimum_channels or mean<minimum_mean:
        raise AssertionError(f"Insufficient pixel difference for {title}")


def invoke(xvfb,exe,out,name,*arguments,model=None):
    path=out/f"v19-{name}.ppm"
    cmd=[xvfb,"-a",exe,"--gui","1","--gizmo","0","--lights","0",
         "--frames","2","--sim-dt","0","--taa","0","--ssao","0",
         "--sun-cascades","0","--point-shadows","0",
         "--auto-exposure","0","--bloom","0",
         "--stats","--screenshot",str(path)]
    if model: cmd+=["--model",str(model)]
    cmd.extend(str(s) for s in arguments)
    result=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    if result.returncode:
        raise AssertionError(f"Execution failed: {result.stdout}\n{result.stderr}")
    def integer(key):
        match=re.search(r"\b"+re.escape(key)+r"=(-?\d+)",result.stdout)
        if not match:
            raise AssertionError(f"Missing stats {key}: {result.stdout}")
        return int(match.group(1))
    return parse_ppm(path),integer,result.stdout


def main():
    xvfb,exe,outdir,fixture=sys.argv[1:]
    out=Path(outdir)
    out.mkdir(exist_ok=True,parents=True)
    triangle=Path(fixture)

    # Corner of mesh-local AABB is empty: v1.8 falsely selected the triangle.
    # The new precise mode must reject the same GLTF primitive.
    bounds,bounds_stats,_=invoke(
        xvfb,exe,out,"bounds-gap","--pick-geometry","bounds",
        "--pick-screen",700,325,model=triangle)
    precise,precise_stats,_=invoke(
        xvfb,exe,out,"triangles-gap","--pick-geometry","triangles",
        "--pick-screen",700,325,model=triangle)
    if bounds_stats("guiSelectedCount")!=1 or precise_stats("guiSelectedCount")!=0:
        raise AssertionError("AABB false positive not removed by triangle-accurate picking")
    if bounds_stats("precisePicking")!=0 or precise_stats("precisePicking")!=1:
        raise AssertionError("Picking mode CLI or scene UI not wired")
    difference(bounds,precise,"AABB vs true triangle in empty corner",0.005,75)

    hit,hit_stats,_=invoke(
        xvfb,exe,out,"triangles-hit","--pick-geometry","triangles",
        "--pick-screen",595,430,model=triangle)
    if hit_stats("guiSelectedCount")!=1:
        raise AssertionError("True triangle ray hit incorrectly rejected")
    difference(precise,hit,"Exact triangle screen hit",0.005,75)

    # Test selection with a smaller screen rectangle, then additive union.
    empty,empty_stats,_=invoke(xvfb,exe,out,"marquee-empty")
    rectangle,small_stats,small_log=invoke(
        xvfb,exe,out,"marquee-partial",
        "--marquee-screen",600,440,690,505)
    count=small_stats("guiSelectedCount")
    if count<2 or count>=26 or "Scene marquee: hits=" not in small_log:
        raise AssertionError(f"Partial marquee selected implausible set: {small_log}")
    difference(empty,rectangle,"Marquee selection bounding frames",0.02,300)

    additive,add_stats,_=invoke(
        xvfb,exe,out,"marquee-additive","--gui-select",13,
        "--marquee-add",600,440,690,505)
    if add_stats("guiSelectedCount")!=count+1:
        raise AssertionError("Additive marquee lost original selection or duplicated a member")
    difference(rectangle,additive,"Additive marquee group",0.004,50)

    full,full_stats,_=invoke(
        xvfb,exe,out,"marquee-all","--marquee-screen",0,0,1279,719)
    if full_stats("guiSelectedCount")!=26:
        raise AssertionError("Full-screen marquee should select all 26 procedural renderables")
    difference(empty,full,"Full viewport rectangular selection",0.02,300)

    for flags in [
        ["--pick-geometry","invalid"],
        ["--marquee-screen",-1,5,40,40],
        ["--marquee-screen",10,10,1280,719],
        ["--marquee-screen",0,0,50],
    ]:
        result=subprocess.run([xvfb,"-a",exe,"--frames","1",
                               *[str(a) for a in flags]],
                              capture_output=True,text=True,timeout=120)
        if result.returncode==0:
            raise AssertionError(f"Malformed picking arguments accepted: {flags}")
    print("Precise triangle hit/miss, rectangular selection, additive union, pixels and CLI validated")


if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""GPU screenshot regression + runtime BVH instrumentation for v2.0.

CPU node/triangle counters are deterministic and independent of CI timing.
"""
from pathlib import Path
import re
import subprocess
import sys


def image(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError("Incorrect screenshot PPM signature")
        width,height=map(int,stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Incorrect screenshot bit depth")
        pixels=stream.read()
    if width<128 or height<128 or len(pixels)!=width*height*3:
        raise AssertionError("Truncated screenshot")
    return (width,height),pixels


def invoke(xvfb,engine,output,name,*flags):
    path=output/f"v20-bvh-{name}.ppm"
    cmd=[xvfb,"-a",engine,
         "--frames","2","--lights","0","--sim-dt","0",
         "--gui","1","--gizmo","0",
         "--taa","0","--ssao","0","--auto-exposure","0",
         "--sun-cascades","0","--point-shadows","0","--bloom","0",
         "--stats","--screenshot",str(path),
         *[str(x) for x in flags]]
    result=subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=120)
    def stat(key):
        match=re.search(r"\b"+re.escape(key)+r"=(-?\d+)",result.stdout)
        if not match:
            raise AssertionError(f"Expected {key} telemetry in {result.stdout}")
        return int(match.group(1))
    return image(path),stat,result.stdout


def equivalent(a,b,title):
    if a[0]!=b[0]:
        raise AssertionError(f"{title}: dimensions changed")
    # The inspector deliberately reports different BVH counts and toggled
    # picking mode. Compare only the scene viewport right of the UI panel.
    width,height=a[0]
    differences=[]
    for row in range(height):
        offset=row*width*3
        left=offset+520*3
        right=offset+width*3
        differences.extend(abs(x-y) for x,y in zip(
            a[1][left:right],b[1][left:right]))
    mean=sum(differences)/len(differences)
    large=sum(d>=5 for d in differences)
    print(f"{title}: mean delta {mean:.4f}, channels >=5 changed: {large}")
    if mean>0.1 or large>len(differences)//1400:
        raise AssertionError(f"{title}: selection image changed after BVH")


def main():
    xvfb,engine,directory,triangle=sys.argv[1:]
    out=Path(directory)
    out.mkdir(parents=True,exist_ok=True)

    bvh,bs,_=invoke(xvfb,engine,out,"sphere-bvh",
                    "--pick-geometry","triangles","--pick-screen",640,467)
    aabb,ab,_=invoke(xvfb,engine,out,"sphere-bounds",
                     "--pick-geometry","bounds","--pick-screen",640,467)
    if bs("guiSelectedIndex")!=23 or bs("guiSelectedCount")!=1:
        raise AssertionError("BVH changed the clicked procedural sphere identity")
    if ab("guiSelectedIndex")!=23 or ab("guiSelectedCount")!=1:
        raise AssertionError("AABB clicked sphere identity unexpectedly changed")
    if bs("precisePicking")!=1 or ab("precisePicking")!=0:
        raise AssertionError("Picking mode reporting changed")
    if bs("pickBVHNodes")<1 or bs("pickTriangles")<1:
        raise AssertionError("BVH traversal counters not integrated with runtime")
    if bs("pickTriangles")>=1500:
        raise AssertionError("BVH did not prune the default 3,072-triangle sphere")
    if ab("pickBVHNodes")!=0 or ab("pickTriangles")!=0:
        raise AssertionError("Bounds-only mode must not visit triangle BVH")
    equivalent(bvh,aabb,"Default sphere BVH/bounds selection")
    print(f"3D sphere hit: {bs('pickBVHNodes')} BVH nodes, "
          f"{bs('pickTriangles')} triangles (3,072 available per sphere)")

    miss,ms,_=invoke(xvfb,engine,out,"empty-sky",
                     "--pick-geometry","triangles","--pick-screen",640,12)
    if ms("guiSelectedCount")!=0 or ms("pickTriangles")!=0:
        raise AssertionError("Empty sky must not visit mesh triangles")

    # Keep v1.9's guarantee that the empty AABB corner is not a triangle.
    nohit,ns,_=invoke(xvfb,engine,out,"triangle-gap",
                      "--model",triangle,"--pick-geometry","triangles",
                      "--pick-screen",700,325)
    oldhit,os,_=invoke(xvfb,engine,out,"triangle-bounds",
                       "--model",triangle,"--pick-geometry","bounds",
                       "--pick-screen",700,325)
    if ns("guiSelectedCount")!=0 or os("guiSelectedCount")!=1:
        raise AssertionError("BVH lost v1.9's empty-triangle rejection")
    hit,hs,_=invoke(xvfb,engine,out,"triangle-surface",
                    "--model",triangle,"--pick-geometry","triangles",
                    "--pick-screen",595,430)
    if hs("guiSelectedCount")!=1 or hs("pickTriangles")<1:
        raise AssertionError("BVH rejected an actual glTF triangle surface")

    # Verify selection overlays change when geometry is actually selected.
    shape=nohit[0]
    assert shape==hit[0]
    changed=sum(abs(x-y)>=8 for x,y in zip(nohit[1],hit[1]))
    if changed<75:
        raise AssertionError("Triangle surface selection not drawn by editor")
    print("BVH telemetry, sphere identity, true glTF ray hits and pixel equivalence passed")


if __name__=="__main__":
    main()

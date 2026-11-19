#!/usr/bin/env python3
"""Headless OpenGL scene ray-picking and multi-selection image regression."""
from pathlib import Path
import re
import subprocess
import sys


def screenshot(path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Invalid P6 screenshot: {path}")
        width, height = map(int, stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError("Invalid RGB screenshot depth")
        pixels = stream.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError("Invalid screenshot dimensions")
    return (width,height),pixels


def call(xvfb, binary, output, name, *flags, count, primary):
    path=output / f"v18-{name}.ppm"
    command=[
        xvfb,"-a",binary,"--gui","1","--gizmo","0",
        "--frames","2","--sim-dt","0","--lights","0",
        "--taa","0","--ssao","0","--auto-exposure","0",
        "--sun-cascades","0","--point-shadows","0","--bloom","0",
        "--stats","--screenshot",str(path), *[str(flag) for flag in flags],
    ]
    process=subprocess.run(command,check=True,capture_output=True,
                           text=True,timeout=120)
    output_text=process.stdout
    for key,expected in [("guiSelectedCount",count),
                         ("guiSelectedIndex",primary)]:
        match=re.search(r"\b"+key+r"=(-?\d+)",output_text)
        if not match or int(match.group(1))!=expected:
            raise AssertionError(
                f"{name}: expected {key}={expected}; got {output_text}")
    if "guiVisible=1" not in output_text:
        raise AssertionError(f"{name}: UI missing")
    return screenshot(path),output_text


def comparison(a,b,label,min_delta,min_channels):
    if a[0]!=b[0]:
        raise AssertionError("Compared screenshot dimensions differ")
    distances=[abs(x-y) for x,y in zip(a[1],b[1])]
    mean=sum(distances)/len(distances)
    changed=sum(v>=8 for v in distances)
    print(f"{label}: mean RGB change={mean:.5f}; channels >=8: {changed}")
    if mean < min_delta or changed < min_channels:
        raise AssertionError(f"{label}: selection was not visibly reflected")


def main():
    xvfb,binary,dirname=sys.argv[1:]
    directory=Path(dirname)
    directory.mkdir(parents=True,exist_ok=True)

    single,one=call(xvfb,binary,directory,"single",
        "--gui-select",13,count=1,primary=13)
    paired,two=call(xvfb,binary,directory,"paired",
        "--gui-select",13,"--gui-add",14,count=2,primary=14)
    comparison(single,paired,"Multi-selected bounding rectangles",0.006,80)

    picked,ray=call(xvfb,binary,directory,"ray",
        "--pick-screen",640,410,count=1,primary=13)
    if "Scene ray pick: Mesh 13" not in ray:
        raise AssertionError(f"Screen ray did not hit expected sphere: {ray}")
    if single[0] != picked[0]:
        raise AssertionError("Manual selection and ray selection viewport sizes differ")
    pixel_differences=[abs(a-b) for a,b in zip(single[1],picked[1])]
    mean_difference=sum(pixel_differences)/len(pixel_differences)
    visually_changed=sum(v>=5 for v in pixel_differences)
    print(f"Manual vs ray picked sphere: mean={mean_difference:.5f}, "
          f"channels>=5: {visually_changed}")
    if mean_difference > 0.08 or visually_changed > len(pixel_differences)//2000:
        raise AssertionError("Ray picking and manual selection show different objects")
    # A second sphere projects near (728,410) at the default FPS camera.
    additive,ray2=call(xvfb,binary,directory,"ray-additive",
        "--pick-screen",640,410,"--pick-add",728,410,
        count=2,primary=14)
    if "Scene ray pick: Mesh 14" not in ray2:
        raise AssertionError(f"Additive ray did not hit second sphere: {ray2}")
    comparison(picked,additive,"Ctrl/Shift group ray-picking",0.006,80)

    # Large empty sky area, outside the editor panel: click clears selection.
    sky,miss=call(xvfb,binary,directory,"empty",
        "--pick-screen",640,12,count=0,primary=-1)
    if "Scene ray pick: (none)" not in miss:
        raise AssertionError("Empty viewport should not select an entity")
    comparison(picked,sky,"Deselect empty sky",0.006,80)

    for flags in [
        ("--gui-add","99999"),("--pick-screen","1280","500"),
        ("--pick-screen","-1","1"),("--pick-screen","1","720"),
    ]:
        result=subprocess.run([xvfb,"-a",binary,"--frames","1",*flags],
                              capture_output=True,text=True,timeout=120)
        if result.returncode==0:
            raise AssertionError(f"Invalid pick command was accepted: {flags}")
    print("Actual OpenGL viewport ray picking, additive selection and image regressions passed")


if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""Run headless TAA on/off, verify history accumulation and compare HDR output."""
from pathlib import Path
import re
import subprocess
import sys


def load_ppm(path: Path):
    with path.open("rb") as file:
        if file.readline() != b"P6\n":
            raise AssertionError(f"Not P6 PPM: {path}")
        width, height = (int(value) for value in file.readline().split())
        if file.readline() != b"255\n":
            raise AssertionError(f"Incorrect PPM bit depth: {path}")
        rgb = file.read()
    if width < 128 or height < 128 or len(rgb) != width * height * 3:
        raise AssertionError(f"Invalid screenshot: {path}")
    return (width, height), rgb


def run(xvfb: str, engine: str, directory: Path, enabled: bool):
    mode = "on" if enabled else "off"
    path = directory / f"taa-{mode}.ppm"
    cmd = [
        xvfb, "-a", engine,
        "--taa", "1" if enabled else "0",
        "--taa-weight", "0.90", "--frames", "8",
        "--lights", "0", "--point-shadows", "0",
        "--sun-cascades", "1", "--ssao", "0", "--bloom", "0",
        "--stats", "--screenshot", str(path),
    ]
    result = subprocess.run(
        cmd, check=True, text=True, capture_output=True, timeout=120
    )
    match = re.search(r"taaHistoryFrames=(\d+)", result.stdout)
    if not match:
        raise AssertionError(f"Missing temporal diagnostics:\n{result.stdout}")
    frames = int(match.group(1))
    expected = 8 if enabled else 0
    if frames != expected:
        raise AssertionError(f"TAA {mode}: expected history {expected}, got {frames}")
    print(f"TAA {mode}: accumulated history frames={frames}")
    return load_ppm(path)


def main():
    xvfb = sys.argv[1]
    engine = sys.argv[2]
    output = Path(sys.argv[3])
    output.mkdir(parents=True, exist_ok=True)
    dimensions_on, pixels_on = run(xvfb, engine, output, True)
    dimensions_off, pixels_off = run(xvfb, engine, output, False)
    if dimensions_on != dimensions_off:
        raise AssertionError("TAA images have mismatched dimensions")
    total = len(pixels_on)
    average = sum(abs(x - y) for x, y in zip(pixels_on, pixels_off)) / total
    changed = sum(abs(x - y) >= 5 for x, y in zip(pixels_on, pixels_off))
    print(f"TAA vs no TAA mean RGB delta={average:.3f}; "
          f"channels changed >=5: {changed}/{total}")
    if average < 0.01 or changed < total // 4000:
        raise AssertionError("Temporal filtering produced no measurable pixel change")


if __name__ == "__main__":
    main()

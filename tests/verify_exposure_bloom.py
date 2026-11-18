#!/usr/bin/env python3
"""Validate actual auto exposure and multiscale bloom in software OpenGL."""
from pathlib import Path
import re
import subprocess
import sys


def ppm(path):
    with path.open("rb") as f:
        if f.readline() != b"P6\n":
            raise AssertionError(f"Not P6 PPM: {path}")
        width, height = map(int, f.readline().split())
        if f.readline() != b"255\n":
            raise AssertionError(f"Invalid bit depth: {path}")
        pixels = f.read()
    if width < 128 or height < 128 or len(pixels) != width * height * 3:
        raise AssertionError(f"Invalid screenshot: {path}")
    return (width, height), pixels


def run(xvfb, engine, destination, name, *, auto, key=0.18,
        bloom=0.0, threshold=1.0):
    path = destination / f"v09-{name}.ppm"
    args = [
        xvfb, "-a", engine, "--frames", "2", "--stats",
        "--auto-exposure", "1" if auto else "0",
        "--exposure-key", str(key),
        "--exposure-speed", "3",
        "--exposure", "1",
        "--bloom", str(bloom),
        "--bloom-threshold", str(threshold),
        "--taa", "0", "--ssao", "0", "--lights", "3",
        "--light-time", "0", "--sun-cascades", "0",
        "--point-shadows", "0",
        "--screenshot", str(path),
    ]
    result = subprocess.run(
        args, check=True, capture_output=True, text=True, timeout=120)
    stats = {}
    for field in ("effectiveExposure", "meteredExposure",
                  "autoExposure", "bloomLevels"):
        match = re.search(r"\b" + field + r"=([\d.]+)", result.stdout)
        if not match:
            raise AssertionError(f"Missing {field} in engine stats:\n{result.stdout}")
        stats[field] = float(match.group(1))
    expected_auto = 1.0 if auto else 0.0
    if stats["autoExposure"] != expected_auto:
        raise AssertionError(f"{name}: wrong auto-exposure mode")
    levels = 5 if bloom > 0.0 else 0
    if stats["bloomLevels"] != levels:
        raise AssertionError(
            f"{name}: expected {levels} active bloom levels, got {stats['bloomLevels']}")
    if not 0.01 <= stats["effectiveExposure"] <= 64.0:
        raise AssertionError(f"{name}: invalid effective exposure {stats}")
    if not auto and abs(stats["effectiveExposure"] - 1.0) > 0.001:
        raise AssertionError(f"{name}: fixed exposure must equal manual multiplier")
    print(f"{name}: {stats}")
    return ppm(path), stats


def compare(name, a, b, min_mean=0.05):
    dims1, pixels1 = a
    dims2, pixels2 = b
    if dims1 != dims2:
        raise AssertionError("Screenshot dimensions differ")
    differences = [abs(x-y) for x, y in zip(pixels1, pixels2)]
    mean = sum(differences) / len(differences)
    changed = sum(d >= 5 for d in differences)
    print(f"{name}: mean RGB delta {mean:.3f}, >=5 changed: {changed}/{len(differences)}")
    if mean < min_mean or changed < len(differences) // 5000:
        raise AssertionError(f"{name} has no meaningful pixel difference")


def main():
    xvfb, engine, target = sys.argv[1:]
    output = Path(target)
    output.mkdir(parents=True, exist_ok=True)
    low, low_stats = run(xvfb, engine, output, "low-key", auto=True, key=0.08)
    high, high_stats = run(xvfb, engine, output, "high-key", auto=True, key=0.38)
    if high_stats["effectiveExposure"] <= low_stats["effectiveExposure"] * 1.25:
        raise AssertionError("Higher exposure key did not raise metered exposure")
    compare("auto-exposure key", low, high, min_mean=0.15)

    fixed, fixed_stats = run(xvfb, engine, output, "manual", auto=False)
    compare("automatic vs fixed exposure", fixed, high, min_mean=0.08)
    bloom, bloom_stats = run(
        xvfb, engine, output, "bloom", auto=False, bloom=1.0, threshold=0.5)
    compare("five-mip bloom on/off", fixed, bloom, min_mean=0.05)

    dim_bloom, _ = run(
        xvfb, engine, output, "bloom-high-threshold",
        auto=False, bloom=1.0, threshold=5.0)
    compare("soft-knee bloom threshold", dim_bloom, bloom, min_mean=0.02)
    print("Auto exposure metering, manual mode and bloom pyramid verified")


if __name__ == "__main__":
    main()

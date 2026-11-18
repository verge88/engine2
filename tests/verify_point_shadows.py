#!/usr/bin/env python3
"""Ensure point-light shadow cubemaps visibly affect a real GPU-rendered image."""
from pathlib import Path
import sys


def ppm(path: Path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Not a P6 screenshot: {path}")
        width, height = (int(value) for value in stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError(f"Wrong bit depth: {path}")
        rgb = stream.read()
    if width < 128 or height < 128 or len(rgb) != width * height * 3:
        raise AssertionError(f"Invalid image size: {path}")
    return (width, height), rgb


if __name__ == "__main__":
    on_size, shadowed = ppm(Path(sys.argv[1]))
    off_size, unshadowed = ppm(Path(sys.argv[2]))
    if on_size != off_size:
        raise AssertionError("Mismatched framebuffer sizes")

    differences = [abs(a - b) for a, b in zip(shadowed, unshadowed)]
    channel_count = len(differences)
    mean = sum(differences) / channel_count
    strong = sum(delta >= 8 for delta in differences)

    print(f"Point-shadow pixel change: mean={mean:.4f}, "
          f"strongly changed channels={strong}/{channel_count}")
    # More than a uniform clear, noise or a single bad pixel is required.
    if mean < 0.025 or strong < channel_count // 4000:
        raise AssertionError(
            "Point-shadow on/off images are too similar; shadow pass may be broken"
        )

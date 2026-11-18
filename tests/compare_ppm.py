#!/usr/bin/env python3
"""Assert that enabling the animated point lights changes actual rendered pixels."""
from pathlib import Path
import sys


def load_ppm(path: Path):
    with path.open("rb") as file:
        if file.readline() != b"P6\n":
            raise ValueError(f"{path}: not a binary PPM")
        size = tuple(int(s) for s in file.readline().split())
        if file.readline() != b"255\n":
            raise ValueError(f"{path}: unsupported color depth")
        pixels = file.read()
    if len(pixels) != size[0] * size[1] * 3:
        raise ValueError(f"{path}: invalid byte length")
    return size, pixels


if __name__ == "__main__":
    a_size, a = load_ppm(Path(sys.argv[1]))
    b_size, b = load_ppm(Path(sys.argv[2]))
    if a_size != b_size:
        raise AssertionError(f"Different framebuffer dimensions: {a_size} vs {b_size}")
    count = len(a)
    mean_difference = sum(abs(x - y) for x, y in zip(a, b)) / count
    changed = sum(1 for x, y in zip(a, b) if abs(x - y) > 5)
    if mean_difference < 0.3 or changed < count // 1000:
        raise AssertionError(
            f"Lighting pipeline changed too few pixels: mean={mean_difference:.3f}, "
            f"channels changed >5={changed}"
        )
    print(f"Point lighting changed {changed} RGB channels, mean delta {mean_difference:.2f}")

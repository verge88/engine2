#!/usr/bin/env python3
"""Guard against a visually inert directional cascade shadow pass."""
from pathlib import Path
import sys


def read_ppm(path: Path):
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError(f"Not a P6 image: {path}")
        width, height = (int(v) for v in stream.readline().split())
        if stream.readline() != b"255\n":
            raise AssertionError(f"Invalid PPM depth: {path}")
        data = stream.read()
    if width < 128 or height < 128 or len(data) != width * height * 3:
        raise AssertionError(f"Invalid captured frame: {path}")
    return (width, height), data


def main() -> None:
    a_size, shadowed = read_ppm(Path(sys.argv[1]))
    b_size, unshadowed = read_ppm(Path(sys.argv[2]))
    if a_size != b_size:
        raise AssertionError("Frame dimensions differ")
    total = len(shadowed)
    large = 0
    sum_delta = 0
    # Only the renderer's directional-shadow settings differ.
    for a, b in zip(shadowed, unshadowed):
        d = abs(a - b)
        sum_delta += d
        large += d >= 8
    mean = sum_delta / total
    print(f"Sun-cascade on/off mean difference: {mean:.4f}, channels changed >=8: {large}")
    if mean < 0.04 or large < total // 3000:
        raise AssertionError("Sun shadows made too little visible difference")


if __name__ == "__main__":
    main()

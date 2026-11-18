#!/usr/bin/env python3
"""Compare actual screenshots of alpha OPAQUE, MASK and BLEND glTF materials."""
from pathlib import Path
import sys


def ppm(path: Path):
    with path.open("rb") as f:
        if f.readline() != b"P6\n":
            raise AssertionError(f"{path} is not P6 PPM")
        width, height = map(int, f.readline().split())
        if f.readline() != b"255\n":
            raise AssertionError(f"{path} is not 8-bit PPM")
        data = f.read()
    if len(data) != width * height * 3:
        raise AssertionError(f"{path} has invalid RGB payload")
    return (width, height), data


def difference(name: str, x: bytes, y: bytes) -> None:
    mean = sum(abs(a - b) for a, b in zip(x, y)) / len(x)
    changed = sum(abs(a - b) >= 5 for a, b in zip(x, y))
    print(f"{name}: mean RGB-channel delta {mean:.2f}; changed={changed}/{len(x)}")
    if mean < 0.35 or changed < len(x) // 200:
        raise AssertionError(f"{name}: rendered alpha modes look unexpectedly identical")


def main() -> None:
    size_opaque, opaque = ppm(Path(sys.argv[1]))
    size_mask, mask = ppm(Path(sys.argv[2]))
    size_blend, blend = ppm(Path(sys.argv[3]))
    if size_opaque != size_mask or size_opaque != size_blend:
        raise AssertionError("Captured images have differing resolutions")
    difference("MASK vs OPAQUE (alpha cutoff/texture)", mask, opaque)
    difference("BLEND vs OPAQUE (linear-HDR blending)", blend, opaque)
    difference("BLEND vs MASK", blend, mask)


if __name__ == "__main__":
    main()

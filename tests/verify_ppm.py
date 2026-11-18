#!/usr/bin/env python3
"""Quick sanity check that the GPU produced nontrivial RGB pixels, not a blank frame."""

import pathlib
import sys


def verify(path: pathlib.Path) -> None:
    with path.open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise AssertionError("Expected a binary PPM (P6) screenshot")
        width, height = (int(value) for value in stream.readline().split())
        if width < 128 or height < 128:
            raise AssertionError(f"Unexpected frame resolution {width}x{height}")
        if stream.readline() != b"255\n":
            raise AssertionError("Expected 8-bit RGB channels")
        pixels = stream.read()

    expected = width * height * 3
    if len(pixels) != expected:
        raise AssertionError(f"Truncated screenshot: {len(pixels)} != {expected}")

    # Avoid dependence on a particular GPU, exposure or antialiasing strategy.
    # A blank framebuffer and a uniform shader failure must both be rejected.
    samples = [
        tuple(pixels[i : i + 3])
        for i in range(0, len(pixels) - 2, 131 * 3)
    ]
    unique = len(set(samples))
    brightest = max(pixels)
    if unique < 20 or brightest < 90:
        raise AssertionError(
            f"Suspicious image content: {unique} colors in sample, max channel={brightest}"
        )
    print(f"Verified {width}x{height} image with {unique} sampled colors")


if __name__ == "__main__":
    verify(pathlib.Path(sys.argv[1]))

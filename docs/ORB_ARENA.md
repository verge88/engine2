# Orb Arena — demo game

`Orb Arena` is a small playable game built entirely on engine2 runtime systems.

## Goal

Collect all **5 Energy Cores** before the two red **Hunter Orbs** remove all
three lives. The player is the blue physical sphere. Arena walls, hunters and
the player use the regular Scene rigid-body solver; hunters and the player have
CCD enabled.

## Controls

- **WASD** — steer the player orb on the XZ plane.
- **Left/Right Shift** — boost to a higher target speed.
- **R** — restart after a win/loss or at any time.
- **Mouse** — look around from the fixed spectator position.
- **F1** — open the normal Scene editor; gameplay pauses while it is open.
- **Esc** — quit.

The window title is the HUD: it shows collected cores, remaining lives and the
win/game-over state.

## Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/engine2 --game-demo 1
```

A deterministic headless smoke capture can be run with:

```bash
xvfb-run -a ./build/engine2 --game-demo 1 --gui 0 \
  --frames 2 --sim-dt 0.0166667 --lights 2 --stats \
  --screenshot orb-arena.ppm
```

## Engine features demonstrated

The game intentionally uses existing engine systems rather than bespoke
collision code:

- fixed 60 Hz Scene physics;
- Sphere/AABB and Sphere/Sphere contacts;
- dynamic-sphere CCD;
- persistent-contact warm starting;
- sphere angular velocity, inertia and friction torque;
- emissive PBR materials, procedural sphere/cube/plane meshes;
- point lights, HDR/bloom/TAA/SSAO through the normal renderer;
- Scene editor pause/debugging via F1.

The gameplay layer only handles steering, pickups, lives, restart rules and HUD
text. Physics remains in the engine.

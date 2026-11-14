# Unreal Engine Playground

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-313131?logo=unrealengine&logoColor=white)](https://www.unrealengine.com/)
[![C++](https://img.shields.io/badge/C%2B%2B-00599C?logo=cplusplus&logoColor=white)](Source/MyProject)
[![Platform](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows&logoColor=white)](#requirements)
[![Git LFS](https://img.shields.io/badge/Git%20LFS-enabled-F64935?logo=git&logoColor=white)](.gitattributes)
[![Last commit](https://img.shields.io/github/last-commit/georglundesgaard/unreal-engine-playground)](https://github.com/georglundesgaard/unreal-engine-playground/commits/main)
[![Pull requests](https://img.shields.io/github/issues-pr-closed/georglundesgaard/unreal-engine-playground)](https://github.com/georglundesgaard/unreal-engine-playground/pulls?q=is%3Apr)

An Unreal Engine 5.8 C++ playground built on Epic's Third Person template. Its main feature is a small, **randomly generated open-world island game** with simple blocky graphics.

## Open-world island game

Each time you start the game, you get a new island built from instanced cubes. The terrain is stepped and has beaches, grassland, forests, rocky slopes and snowy peaks, surrounded by ocean. Trees, boulders, flowers and clouds are scattered across it.

**Goal:** find all 8 glowing crystals. Each crystal has a tall light beam so you can spot it from a distance, and every crystal is placed where the player can actually reach it.

- The HUD shows the crystal count, a timer, the world seed, a hint pointing to the nearest crystal, and a minimap.
- If you walk into deep water, you are put back on the last dry ground you stood on.
- Every slope can be climbed: neighbouring land tiles differ by at most 80 cm, which is within the character's jump height.

### Controls

| Input | Action |
|---|---|
| `W` `A` `S` `D` | Move |
| Mouse | Look |
| `Space` | Jump |
| `R` | Return to the starting point |
| `N` | Generate a brand-new world |
| `T` | Switch between smooth and blocky terrain. This reloads the same island, so collected crystals and the timer reset. |

To replay a specific island, open the map with a seed, for example `?Seed=1234` in the map URL. `?Style=Blocky` or `?Style=Smooth` picks the terrain style. In the editor, the console variables `OpenWorld.Seed <n>` and `OpenWorld.TerrainStyle <0|1>` (0 = Blocky, 1 = Smooth, -1 = default) force the seed or style for the next Play or Simulate session.

### Terrain styles

| Smooth (default) | Blocky |
|---|---|
| One continuous procedural mesh. Each corner's height is the average of the tiles around it, then blurred. Heights above the water line are then stretched (`HillScale`), which gives tall hills without moving the coastline, and small surface bumps are added (`DetailNoise`). The mesh has smooth normals, and its vertex colours blend biomes into soft gradients. | One instanced cube column per tile, giving a stepped voxel look with hard biome edges. |
| **Detailed props:** oaks (root flare, branches, clustered crowns, some in autumn colours), white birches, layered pines that get snow caps high up, bushes with berries, grass tufts, flowers on stems, mushrooms, fallen logs and stumps, boulder clusters with pebbles, reeds and cattails along the shore, and fluffy sphere clouds. Small props are not drawn beyond 70 m. | **Simple props:** cube trees, cone pines, cube boulders and flowers, and box clouds. |

Both styles come from the same height map, so crystals and the spawn point stay in the same places when you switch. Vegetation is generated in each style's own look. Everything is placed on whichever ground surface is active.

### How it works

| Class | Responsibility |
|---|---|
| [`AOpenWorldGenerator`](Source/MyProject/Variant_OpenWorld/OpenWorldGenerator.h) | Builds the height map with fractal Perlin noise plus an island falloff, limits slopes, assigns biomes, builds the terrain (a smooth procedural mesh or blocky instanced cubes), places the spawn point and crystals (using a search for tiles the player can reach), scatters decoration into instanced mesh layers, and creates the minimap texture |
| [`AOpenWorldCrystal`](Source/MyProject/Variant_OpenWorld/OpenWorldCrystal.h) | Spinning, glowing collectible with a light beam |
| [`AOpenWorldGameMode`](Source/MyProject/Variant_OpenWorld/OpenWorldGameMode.h) | Chooses the seed, generates the world, spawns sky and lighting when the level has none, tracks the objective, and handles deep-water resets and the `R`/`N` keys |
| [`AOpenWorldHUD`](Source/MyProject/Variant_OpenWorld/OpenWorldHUD.h) | HUD drawn on the canvas: counter, timer, seed, nearest-crystal hint and minimap |

All visuals use engine basic shapes (`/Engine/BasicShapes`) with colour-tinted material instances. The only exception is the smooth terrain, which uses a small vertex-colour material (`M_TerrainVertexColor`) and the `ProceduralMeshComponent` plugin. The game needs no other art assets. The player character and input come from the template's `BP_ThirdPersonCharacter` and `BP_ThirdPersonPlayerController`.

## Other template variants

The original template variants are still included and can be played from their own levels:

| Level | Variant |
|---|---|
| `Content/ThirdPerson/Lvl_ThirdPerson` | Basic third-person movement |
| `Content/Variant_Combat/Lvl_Combat` | Melee combos, charged attacks, StateTree-driven enemy AI |
| `Content/Variant_Platforming/Lvl_Platforming` | Dash, wall jump and platforming |
| `Content/Variant_SideScrolling/Lvl_SideScrolling` | Side-scroller with NPCs, jump pads and moving platforms |

## Requirements

- Unreal Engine **5.8**
- Windows with Visual Studio 2022 (C++ game development workload)
- [Git LFS](https://git-lfs.com/), because all `.uasset`/`.umap` files are stored in LFS

## Getting started

```sh
git lfs install
git clone git@github.com:georglundesgaard/unreal-engine-playground.git
cd unreal-engine-playground
```

Build the editor target, then open the project:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=$PWD\MyProject.uproject" -WaitMutex
```

You can also right-click `MyProject.uproject`, choose **Generate Visual Studio project files**, and build from the solution. The editor opens on `Lvl_OpenWorld`; press **Play** to start the game.

> Live Coding cannot add new C++ classes or change headers. After such changes, close the editor and rebuild the editor target.

## Project layout

```
Config/                       Project settings (startup map, game mode, asset manager)
Content/Variant_OpenWorld/    Lvl_OpenWorld map, M_TerrainVertexColor material
Content/...                   Template characters, input and variant levels
Source/MyProject/
  Variant_OpenWorld/          Open-world island game (generator, crystal, game mode, HUD)
  Variant_Combat/             Combat template variant
  Variant_Platforming/        Platforming template variant
  Variant_SideScrolling/      Side-scrolling template variant
```

## Editor automation

The project enables Unreal's **Model Context Protocol** plugin with all toolsets. When the editor is running, AI agents such as Claude Code can control it through MCP: `.mcp.json` points them at the editor's MCP server, `http://127.0.0.1:8000/mcp`.

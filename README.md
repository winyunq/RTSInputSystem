# RTS Input System

`RTSInputSystem` is the public RTS interaction layer used by Mass Battle Frame. It provides a complete minimum playable interface without importing Winyunq's game missions, factions, resources, technology trees, maps, or unit content.

## Public release scope

The repository includes:

- smooth RTS camera movement, edge scrolling, ground-height adaptation, camera bounds, follow and jump-to;
- mouse, keyboard and gamepad input through Enhanced Input;
- Actor and Mass entity selection, marquee selection, selection queries and ten control groups;
- move, attack, stop, hold, patrol and extensible command dispatch;
- a 3x5 command grid, unit roster, unit detail, formation, active-group and command-progress widgets;
- minimap coordinate conversion, camera-frustum drawing and minimap click/right-click input;
- plugin Content, configuration, source code and migration redirects required by the public runtime;
- `ARTSMinimalGameMode`, a native minimal HUD entry point that requires no game-specific Widget Blueprint.

The public minimum does not include Winyunq's missions, economy, named factions, technology tree, campaign UI, maps, production data, or proprietary unit definitions. Projects provide those systems through the public selection, command, progress and widget extension points.

Advanced game-specific HUD composition, large-project integration and deeper customization are separate integration work and are not required to run the public minimum.

## Quick start: see the complete minimum HUD

1. Clone this repository into `<Project>/Plugins/RTSInputSystem`.
2. Enable `RTS Input System` and its declared plugins, then restart Unreal Editor.
3. Open a map and set **World Settings > GameMode Override** to **RTS Minimal Game Mode** (`ARTSMinimalGameMode`).
4. Press Play.

The native HUD immediately exposes the quick-selection bar, minimap input surface, unit/formation panel, active group and command grid. Empty panels remain visible until the project supplies selectable units and command data, so installation can be verified without copying assets from the Winyunq game.

Custom projects may subclass `ARTSMinimalGameMode`, `ARTSMinimalHUD`, `ARTSMinimalPlayerController` or `URTSMinimalHUDWidget`. The native child widgets can also be placed directly in a custom UMG HUD.

## Content Browser visibility

The plugin descriptor sets `CanContainContent` to `true`, and the complete `Content/` directory is part of this repository. If plugin assets do not appear in Unreal Editor, open the Content Browser settings and enable **Show Plugin Content**. The assets then appear under **Plugins > RTS Input System Content**.

Do not copy the repository's `.git` directory or a GitHub HTML page into `Plugins`. Clone or download the repository itself and confirm that this path exists:

```text
<Project>/Plugins/RTSInputSystem/RTSInputSystem.uplugin
```

## Dependencies

`RTSInputSystem` declares these Unreal/plugin dependencies:

- `EnhancedInput`
- `MassBattle`
- `MassGameplay`
- `ProceduralMeshComponent`

`FogOfWar` and the historical `OpenRTSCamera` plugin are not dependencies of `RTSInputSystem`. Camera code is already part of this repository.

`FogOfWar` is an optional, separate plugin. Installing it may replace the minimal HUD's placeholder minimap renderer, but selection, camera control, commands and minimap input do not require it.

## FogOfWar integration

The two plugins share only a map-coordinate protocol. They do not reference each other as C++ modules or `.uplugin` dependencies.

Create the following file when the map should use explicit bounds:

```text
Config/MapRegion/<MapName>/MapRegion.ini
```

```ini
[MapRegion]
OriginX=0
OriginY=0
SizeX=409600
SizeY=409600
MapOverflowUU=0
```

`URTSCamera` uses those bounds for camera limits, while minimap systems use the same values for coordinate projection. When the file is absent, the public input widgets use safe default bounds.

## Runtime architecture

- `URTSCamera` owns camera motion and map-bound resolution.
- `URTSSelector` and `URTSSelectionSubsystem` own selection state, query selection and control groups.
- `URTSCommandSubsystem` owns command resolution and dispatch.
- `IRTSCommandInterface` lets Actor-backed selections expose custom command grids.
- Mass selections use `FEntityHandle` identities and the Mass unit command path.
- `IRTSCommandProgressProvider` publishes production, research, construction or other timed progress without coupling the input plugin to a game economy.

Default Mass unit commands use these gameplay tags:

```text
RTS.Command.Move
RTS.Command.Attack
RTS.Command.Stop
RTS.Command.Hold
RTS.Command.Patrol
```

## Controls

- `Ctrl + 0-9`: replace a control group with the current selection.
- `Shift + 0-9`: add to or remove from a control group.
- `Alt + 0-9`: remove the selected units from other groups and replace the target group.
- `0-9`: recall a group.
- Double-tap `0-9`: recall and center the RTS camera on the dominant unit cluster.

Project input mappings may extend or replace the defaults.

## Historical naming and migration

The repository and plugin folder are named `RTSInputSystem`; the runtime module and script path are also `RTSInputSystem` and `/Script/RTSInputSystem`.

The former standalone `RTSCommandSystem` runtime was moved into this repository. `Config/DefaultEngine.ini` contains redirects from `/Script/RTSCommandSystem` so existing command assets can migrate. No `OpenRTSCamera` package is required, and this release does not preserve duplicate historical camera asset paths.

## Compatibility

The current public branch targets Unreal Engine 5.8 on Win64. The plugin source is included so downstream projects can build it with their Unreal Engine toolchain.

## License and commercial customization

The repository license governs the public minimum described above. A production game may need custom HUD art, faction/economy integration, specialized commands, profiling and project-specific optimization; contact `winyunq@gmail.com` for commercial deep customization.

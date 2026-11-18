# Parity: where Sparxel stands

> **What this is.** Every feature of the scope ([`plan/02`](../plan/02-feature-scope.md)),
> the engine that does it best today, and where Sparxel is: **done**, **partial** (what
> is missing is named) or **not yet**. Measured against the code and its tests, not
> against plans. Kept up to date as features land; the gaps here are what is worked on
> next.
>
> Reference engines, each named for what it is best at: **RPG Maker MZ** (RPG kit,
> event-driven maps, window skins), **Aseprite** (pixel art and animation), **LDtk** and
> **Tiled** (map making, auto-tiling), **Godot** (scene editing, inspector, in-editor
> play), **Unity** (prefabs, inspector), **GDevelop** and **Construct** (no-code event
> logic, object groups and families), **GameMaker** (2D workflow), **GB Studio**
> (no-code for beginners), and **Octopath Traveler**'s HD-2D (the look).

## Summary

| Area | Done | Partial | Not yet |
|---|---|---|---|
| A. Project and workflow | 5 | 7 | 0 (+1 later) |
| B. Pixel art | 4 | 7 | 3 (+1 later) |
| C. World building | 4 | 7 | 3 |
| D. Rendering and the look | 4 | 10 | 1 (+1 later) |
| E. No-code logic | 3 | 5 | 3 (+1 later) |
| F. Genre kits | 4 | 0 | 2 |
| G. Story | 5 | 2 | 0 (+1 later) |
| H. Characters | 3 | 2 | 0 |
| I. Physics | 3 | 2 | 0 |
| J. Audio | 2 | 4 | 0 (+1 later) |
| K. UI | 3 | 4 | 0 |
| **All** | **40** | **50** | **12** (+6 later) |

The strongest areas match or pass their references: the RPG kit and window skins (RPG
Maker), auto-tiles and tile variants (RPG Maker, LDtk), no-code Rules with tags, *it*,
*For each* and *who* (GDevelop, Construct), the editor's everyday workflow (search
everything, snapshots, copy and paste, playing in the editor: Godot), and the HD-2D look
(lights, tilt-shift, bloom, day and night). The widest gaps are in pixel-art authoring
(layer groups, per-frame pivots, normal maps: Aseprite), 2D platformer and
action kits (GameMaker), audio mixing (zones, reverb), and team features.

## A. Project and workflow

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| A1 | Project hub | Godot's project manager | **Partial**: a start page with recent projects and the sample; no thumbnails of projects |
| A2 | Project wizard | GDevelop's | **Partial**: name and template (Classic 2D, Diorama, Platformer, Visual novel, RPG); resolution, PPU and palette are set after, in Project |
| A3 | Templates | RPG Maker's sample games | **Partial**: playable RPG, action, platformer, visual novel and top-down templates; none yet for tactics |
| A4 | Asset browser | Unity's Project window | **Partial**: thumbnails, search, find usages, safe rename (with companions and references); no tags, favourites or move between folders |
| A5 | Undo and redo everywhere | Godot | **Partial**: one history for scenes, logic and settings, Sprite Studio and the Story editor each their own; no visual history list |
| A6 | Autosave and crash recovery | Unity | **Done**: unsaved work kept every minute and offered back after a crash |
| A7 | Snapshots | GameMaker's backups | **Done**: named restore points with pictures (as copies, not Git) |
| A8 | Git integration | Godot (plugin) | **Done**: start keeping versions, what changed, save a version with words, the history; a .gitignore for the editor's working files |
| A9 | Team features | Unity | **Partial**: versions shared with Git from the editor, files that are text, sorted and merge-friendly; no live editing together, locks or review |
| A10 | Project Doctor | none as broad | **Done**: missing assets, broken references, logic errors, the style guide's palette, the font |
| A11 | Style guide | none built in | **Partial**: project palette (new sprites, checked), PPU, font; no outline rules |
| A12 | Asset packs | Unity packages | **Done**: export files or folders with everything they use, bring a pack into another project with its GUIDs, same files kept and conflicts refused |
| A13, A14 | Marketplace, cloud sync | | Later (post 1.0 in the plan) |

## B. Pixel art (Sprite Studio)

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| B1 | Drawing tools | Aseprite | **Done**: pencil, eraser, line, rectangles and ellipses, fill (joined or all of a colour), selection by rectangle, magic wand and lasso (any shape, which every tool keeps to), move, cut, copy, paste, flips, colour picker |
| B2 | Pixel-art helpers | Aseprite | **Partial**: pixel-perfect strokes, mirroring across and down, dither and shade brushes along the palette, an outline generator; no tile and wrap mode |
| B3 | Layers | Aseprite | **Partial**: add, name, order, opacity, hide, merge, duplicate; no groups, blend modes or reference layers |
| B4 | Palettes | Aseprite, GameMaker | **Partial**: the sprite's palette, the project's palette, palette swaps per sprite with a hue-shift picker; no palette library or ramps editor |
| B5 | Animation timeline | Aseprite | **Done**: frames with their own times, tags with forward, backward and ping-pong, onion skin, playback |
| B6 | Frame events | Spine | **Done**: named on frames in Sprite Studio, kept in the sheet, sent to Rules as the frame comes |
| B7 | Hitboxes and pivots per frame | GameMaker | **Partial**: a hit and a hurt box per frame, drawn in Sprite Studio, raising *hit* and *hits* in the game; one pivot per sprite, not per frame |
| B8 | Directional sets | RPG Maker character sheets | **Partial**: 4-direction walk rows (Walk Animation); no 8 directions or mirroring |
| B9 | Lighting maps | Sprite Illuminator | **Not yet** |
| B10 | Slices and 9-slice | Aseprite | **Partial**: 9-slice drawing (Screen Pictures, box frames); slices not authored in Sprite Studio |
| B11 | Atlas packing | Unity, GameMaker | **Not yet** (one sheet per sprite) |
| B12 | Import | Aseprite → engines | **Done**: PNG, sheets, `.aseprite` with tags and live reload |
| B13 | Character generator | RPG Maker | **Not yet** |
| B14 | 3D-to-pixel baker | | Later |
| B15 | Tile authoring and auto-tile rules | LDtk, Tiled | **Partial**: RPG Maker-style 2 × 3 blocks expanded to 48 shapes, tile variants; no Wang or rule templates |

## C. World building

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| C1 | Diorama builder | Crocotile 3D | **Partial**: tile heights build terrain shown in 3D; no painting onto faces |
| C2 | Block brushes | Crocotile 3D | **Partial**: raise and lower tiles; no slopes, stairs or arches |
| C3 | Prefabs | Unity | **Done**: made from entities, placed, updated in every copy, created by logic |
| C4 | Auto-tiling rules | LDtk | **Partial**: auto-tiles and variants; no rule editor |
| C5 | Scatter and variation | LDtk, Tiled | **Done**: a scatter brush with density, random variants |
| C6 | 2D map mode | Tiled | **Partial**: tile layers by maps, parallax pictures; no parallax for whole tile layers |
| C7 | Entities | Godot | **Done**: typed properties from reflection, prefabs, a hierarchy |
| C8 | Regions and zones | RPG Maker regions | **Partial**: trigger and encounter areas, Sound Sources; no camera or Look zones |
| C9 | Water, sky, weather | Octopath | **Partial**: rain, snow, fog and animated tiles; no sky presets |
| C10 | Navigation | Godot | **Partial**: grid A* (walkers find their way round walls); no navmesh |
| C11 | Map connections | RPG Maker | **Done**: doors both ways, fades, arriving at doors |
| C12 | glTF import | Godot | **Not yet** |
| C13 | Large worlds | Godot | **Not yet** |
| C14 | Terrain | | **Not yet** (tile heights only) |

## D. Rendering and the look

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| D1 | Render styles | Octopath (HD-2D) | **Partial**: Diorama and Classic 2D; no Retro 3D |
| D2 | Billboard sprites | Octopath | **Partial**: upright billboards, depth-correct; other modes not yet |
| D3 | Lights | Octopath | **Partial**: point lights in both styles with sun and moon; no spot or area lights, cookies, or flicker presets |
| D4 | Sprite shadows | Octopath | **Done**: round or cast shadows, block shadows |
| D5 | Time of day | Octopath | **Done**: a clock with dawns, dusks and nights |
| D6 | Post-processing | Unity URP | **Partial**: tilt-shift, bloom, grading, vignette, fog; no SSAO, grain, CRT |
| D7 | Look presets | Octopath | **Partial**: seven presets, previewed by hovering; no zone or time blending |
| D8 | Texel density | | **Partial**: one PPU for the project, every sprite pixel-snapped; no checker for art drawn at another density |
| D9 | Filtering modes | | **Partial**: nearest; crisp-AA and bilinear not yet |
| D10 | Camera rigs | Unity Cinemachine | **Partial**: follow, dead zone, look-ahead, shake, smoothing, limits; no rails or cutscene cameras |
| D11 | Parallax and skyboxes | GameMaker | **Partial**: parallax pictures in Classic 2D, scrolling at any speed and repeating; no pixel skyboxes for the Diorama style |
| D12 | Pixel particles | GameMaker | **Done**: fire, smoke, rain, snow, dust, magic, sparks |
| D13 | Material effects | GameMaker shaders | **Partial**: palette swap, tint, flash, outline and dissolve in both styles; no scrolling textures or custom shaders |
| D14 | Quality tiers | Unity | **Done**: the player's Graphics option, High, Medium (no bloom, no sun shadow map) and Low (no tilt-shift either), kept with their options |
| D15, D16 | Shader graph, palette-locked lighting | | Later |
| D17 | Baked lighting | Unity | **Not yet** |

## E. No-code logic

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| E1 | Behaviors library | GDevelop | **Done**: movers, walkers, jumping, physics bodies, triggers, interactables, animation, enemies on the map, sound sources |
| E2 | Links | Fortnite UEFN devices | **Not yet** (Rules do it) |
| E3 | Rules | GDevelop, Construct | **Done**: When / If / Do / Otherwise, tags (*any object tagged…*), *it*, *For each*, *who* |
| E4 | Sequences | RPG Maker events | **Partial**: steps with waits, ifs and repeats; no timeline view |
| E5 | Brains | Unity Behavior | **Partial**: state machines written as logic; no graph editor |
| E6 | Variables | GDevelop | **Partial**: typed, saved with the game; per-object variables not yet |
| E7 | Expressions | GDevelop | **Partial**: values from properties, events, variables, arithmetic; no formula editor |
| E8 | Custom blocks | GDevelop extensions | **Not yet** |
| E9 | Messages, timers, spawners | Construct | **Done** |
| E10 | Live logic debugger | GDevelop | **Partial**: lines keep their blocks and the VM traces them; no breakpoints or watches in the editor |
| E11 | Luau scripting | Roblox | **Not yet** in the editor (the runtime runs Luau) |
| E12 | Rewind debugging | | Later |

## F. Genre kits

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| F1 | Common: dialogue, inventory, shops, quests, saves, minimap, menus, options, credits, achievements | RPG Maker | **Done** |
| F2 | RPG | RPG Maker | **Done**: party, levels and growth, skills, items, equipment, status effects, turn-based battles, encounters, enemies on the map |
| F3 | Tactics | SRPG Studio | **Not yet** |
| F4 | Action-adventure | GameMaker | **Done**: health that hit boxes take, a blink of safety, knockback, defeat; enemies that chase on sight; an Attack action; a template with a sword, slimes and a health bar |
| F5 | Platformer | GameMaker | **Done**: jumping with coyote time, an early press and variable height, moving platforms that carry the player, ladders, solid half tiles and slopes, a template |
| F6 | Adventure | Adventure Game Studio | **Not yet** (interactables and inventory cover part) |

## G. Story

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| G1 | Dialogue graph | Yarn, articy | **Done**: the Story editor |
| G2 | Screenplay mode | | **Done**: conversations written as a script (speakers, passages, choices, conditions and effects as notes), applied to the graph as one undo step and written back from it exactly |
| G3 | Speakers | RPG Maker | **Done**: portraits, name plates, voice blips |
| G4 | Text effects | RPG Maker | **Done**: colour, wave, shake, speed, pauses |
| G5 | Quests | | **Done**: stages, journal |
| G6 | Story outline | | **Partial** (conversations, flags; no chapters view) |
| G7 | Localization | Unity Localization | **Partial**: tables, CSV, pseudo language, the player's choice; no XLIFF or per-language fonts |
| G8 | ink and Yarn import | | Later |

## H. Characters

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| H1 | Character asset | RPG Maker | **Partial**: prefabs gather sprite, collision, behaviours; no one-place editor |
| H2 | NPC behaviours | RPG Maker | **Done**: wander, patrol, follow, walk to |
| H3 | Enemy AI presets | | **Done**: Walkers that stand, wander or patrol, and chase or flee on seeing the player past walls; enemies on the map that start battles |
| H4 | Party followers | RPG Maker | **Partial**: *Follow* walks behind; no caterpillar of the party |
| H5 | Pathfinding | Godot | **Done** for grids (A*); navmesh not yet |

## I. Physics

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| I1 | Collision | Tiled, Godot | **Partial**: solid tiles, half tiles, slopes, bodies; no sprite-alpha shapes |
| I2 | Collision layers | Godot | **Done**: by the tags objects already have: Solid Bodies that block only some, Trigger Areas that notice only some, Health hurt only by some |
| I3 | Character controllers | Godot | **Partial**: top-down and side-view movers |
| I4 | Triggers and overlaps | Construct | **Done**: trigger areas, touches, hit and hurt boxes, *is tagged*, *can see* (a raycast past solid tiles) and *distance* in Rules, line of sight for Walkers |
| I5 | Rigid-body props | Godot | **Done**: Box2D bodies to push, fall and bounce |

## J. Audio

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| J1 | Import | | **Done**: WAV, FLAC, MP3, Ogg |
| J2 | SFX generator | sfxr | **Done** |
| J3 | Music | FMOD | **Partial**: loops, fades, crossfades; no adaptive layers or stingers |
| J4 | Ambience and footsteps | | **Partial**: footsteps by surface, sound sources; no reverb zones |
| J5 | Mixer | | **Partial**: music and sound buses, ducking under messages; no snapshots |
| J6 | Spatial audio | | **Partial**: sources panned and quieter with distance |
| J7 | FMOD, Wwise | | Later |

## K. UI

| ID | Feature | Best today | Sparxel |
|---|---|---|---|
| K1 | UI designer | Godot Control nodes | **Partial**: anchors, stack and grid layouts, 9-slice pictures, themes, dragged in the scene view; no buttons, nested layouts |
| K2 | Widget library | Godot | **Partial**: text, bars, pictures; no buttons, sliders, lists the player uses |
| K3 | Templates | RPG Maker | **Done**: title, pause, options, save and load, items, equipment, shop, battle, dialogue box, quest log |
| K4 | Navigation and prompts | | **Done**: keyboard and gamepad menus, button prompts for the last device |
| K5 | Pixel-perfect UI and bitmap fonts | | **Done**: whole-number scaling, the game's own pixel font |
| K6 | Accessibility options | | **Partial**: text speed, volumes, key remapping (Jump and Attack too), reduced flashing (no hurt flashes, blinking or camera shakes); no text size |
| K7 | UI animations | RPG Maker | **Partial**: boxes open and fade, the typewriter; no tweens to author |

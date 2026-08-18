<div align="center">
    <img src="textures/base/pack/logo.png" width="20%">
    <h1>Axis</h1>
    <p><i>A voxel game engine, forked from Luanti.</i></p>
    <a href="https://github.com/neitansh/axis-engine/actions/workflows/linux.yml"><img src="https://github.com/neitansh/axis-engine/actions/workflows/linux.yml/badge.svg" alt="Linux build"></a>
    <a href="https://github.com/neitansh/axis-engine/actions/workflows/windows.yml"><img src="https://github.com/neitansh/axis-engine/actions/workflows/windows.yml/badge.svg" alt="Windows build"></a>
    <a href="https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html"><img src="https://img.shields.io/badge/license-LGPLv2.1%2B-blue.svg" alt="License"></a>
</div>
<br>

> **This is not the Luanti repository.** Axis is a fork of
> [Luanti](https://github.com/luanti-org/luanti) with its own goals, its own
> release schedule and its own engine changes. If you are looking for Luanti
> itself, go to <https://www.luanti.org/>.

## What is Axis?

Axis is a voxel game engine. It builds as `axis` (client) and `axisserver`
(dedicated server), exposes its Lua API under the `axis` namespace, and is
developed as a single engine plus the games built on top of it — rather than as
a general-purpose platform for arbitrary third-party content.

The engine is written in C++ with a Lua modding API, and runs on GNU/Linux,
Windows, macOS and Android.

## Relationship with Luanti

```
Luanti (upstream, https://github.com/luanti-org/luanti)
   │
   └── Axis — this repository
```

Axis was forked from Luanti in July 2026 and keeps the upstream history intact.
Most of the engine — the map, the network protocol, the renderer, the Lua
scripting layer — is Luanti code, written by the Luanti contributors and
licensed under LGPL-2.1-or-later. Axis adds to it and changes parts of it; it
does not replace it, and it does not claim authorship of it.

What that means in practice:

- **Files that came from Luanti keep their Luanti copyright headers.** Look for
  `// Luanti` on the first line.
- **Files written for Axis carry `// Axis`** and a copyright line naming the
  Axis contributors.
- **Bug reports about Axis belong here**; bugs that also exist in unmodified
  upstream code are better reported to Luanti, where they will help everyone.
- **Upstream documentation stays authoritative** for everything Axis has not
  changed. Where this repository links to `luanti.org` or `docs.luanti.org`, it
  is pointing at upstream resources on purpose.

Axis is not affiliated with or endorsed by the Luanti project.

## What Axis changes

The list below is limited to what actually exists in this repository today.

**Configuration.** Settings live in a directory of files grouped by who reads
them (`config/shared/`, `config/client/`, `config/server/`) instead of one flat
`minetest.conf`. See [doc/configuration.md](doc/configuration.md).

**Bedrock/Blockbench models.** `.geo.json` geometry and `.animation.json`
animation files load as ordinary meshes, with named animation tracks, bone
overrides and Molang expressions. See [doc/bedrock_models.md](doc/bedrock_models.md)
and the conformance tests in `src/unittest/test_bedrock.cpp`.

**First-person objects.** An object attached to a player can declare that its
place is at the owner's camera while the owner is in first person, and on its
attachment bone for everyone else — enough to build a viewmodel out of entities.

**Camera impulses.** Recoil, blast and shake are offsets applied on top of the
player's own look direction, integrated client-side at a fixed timestep, instead
of server-side changes to the player's rotation.

**Server-to-server transfer.** A server can hand a player over to another server
without the player leaving the world.

**Matchmaking in the main menu.** The "join game" screen has a matchmaking mode
next to the server list. The public Luanti server list is not used.

**Network diagnostics.** A client-side recorder for packet timing, plus a
server-side impairment layer for reproducing bad links on purpose.


## What this repository does not contain

Axis is the engine. The games that run on it, the matchmaking service and the
servers behind it are developed separately and are not part of this repository.
A clone of this repository builds a working engine, not a playable product —
the same way Luanti builds without a game.

That split is deliberate and it is not about licensing: LGPL-2.1 covers the
engine and does not reach the games loaded into it.

## Building

- [Compiling — common information](doc/compiling/README.md)
- [Compiling on GNU/Linux](doc/compiling/linux.md)
- [Compiling on Windows](doc/compiling/windows.md)
- [Compiling on macOS](doc/compiling/macos.md)

Docker:

- [Developing the server with Docker](doc/developing/docker.md)
- [Running a server with Docker](doc/docker_server.md)

## Documentation

- [doc/](doc/) — reference documentation in this repository
- [Developer documentation](doc/developing/)
- [Lua API](doc/lua_api.md) — server modding
- [Client Lua API](doc/client_lua_api.md), [SSCSM API](doc/sscsm_api.md)
- [Configuration](doc/configuration.md)

Upstream documentation, which still applies to everything Axis has not changed:

- Luanti website: <https://www.luanti.org/>
- Luanti documentation: <https://docs.luanti.org/>

## Paths

Locations:

* `bin`   — compiled binaries
* `share` — distributed read-only data
* `user`  — user-created modifiable data

Where each location is on each platform:

* Windows `.zip` / `RUN_IN_PLACE` source:
    * `bin`   = `bin`
    * `share` = `.`
    * `user`  = `.`
* Linux installed:
    * `bin`   = `/usr/bin`
    * `share` = `/usr/share/axis`
    * `user`  = `~/.minetest` or `$LUANTI_USER_PATH`
* macOS:
    * `bin`   = `Contents/MacOS`
    * `share` = `Contents/Resources`
    * `user`  = `Contents/User`, `~/Library/Application Support/minetest`
      or `$LUANTI_USER_PATH`

The user directory and the `LUANTI_USER_PATH` variable keep the names they have
upstream. Renaming them would strand existing installations, so Axis has not
done it.

Worlds are separate folders in `user/worlds/`.

## Configuration

- Default location: `user/config/`
- The configuration is a directory of files rather than a single one. Each file
  holds one subject, inside a section that says who reads it:

    ```
    config/shared/    engine.conf  logging.conf
    config/client/    graphics.conf  audio.conf  input.conf  keybindings.conf
                      interface.conf  network.conf  session.conf  custom.conf
    config/server/    server.conf  network.conf  gameplay.conf  security.conf
                      performance.conf  worldgen.conf  custom.conf
    ```

- A dedicated server reads and writes `shared/` and `server/` only. A client
  reads all three, because it hosts the server of a single player game, but it
  writes every value into the file of its own subject.
- The files are created on the first run, empty apart from a comment saying what
  belongs in them. `config.example/` documents every setting with its default.
- A specific directory can be specified on the command line:
    `--config-dir <path-to-directory>`
- Settings of games and mods, which the engine does not know, go to
  `custom.conf` of the running side.
- Details, and how to add a setting: [doc/configuration.md](doc/configuration.md)

## Command-line options

- Use `--help`

## Default controls

All controls are re-bindable using settings.
Some can be changed in the key config dialog in the settings tab.

| Button                        | Action                                                         |
|-------------------------------|----------------------------------------------------------------|
| Move mouse                    | Look around                                                    |
| W, A, S, D                    | Move                                                           |
| Space                         | Jump/move up                                                   |
| Shift                         | Sneak/move down                                                |
| Q                             | Drop itemstack                                                 |
| Shift + Q                     | Drop single item                                               |
| Left mouse button             | Dig/punch/use                                                  |
| Right mouse button            | Place/use                                                      |
| Shift + right mouse button    | Build (without using)                                          |
| I                             | Inventory menu                                                 |
| Mouse wheel                   | Select item                                                    |
| 0-9                           | Select item                                                    |
| Z                             | Zoom (needs zoom privilege)                                    |
| T                             | Chat                                                           |
| /                             | Command                                                        |
| Esc                           | Pause menu/abort/exit (pauses only singleplayer game)          |
| +                             | Increase view range                                            |
| -                             | Decrease view range                                            |
| K                             | Enable/disable fly mode (needs fly privilege)                  |
| J                             | Enable/disable fast mode (needs fast privilege)                |
| H                             | Enable/disable noclip mode (needs noclip privilege)            |
| E                             | Aux1 (Move fast in fast mode. Games may add special features)  |
| C                             | Cycle through camera modes                                     |
| V                             | Cycle through minimap modes                                    |
| Shift + V                     | Change minimap orientation                                     |
| F1                            | Hide/show HUD                                                  |
| F2                            | Hide/show chat                                                 |
| F3                            | Disable/enable fog                                             |
| F4                            | Disable/enable camera update (Mapblocks are not updated anymore when disabled, disabled in release builds)  |
| F5                            | Cycle through debug information screens                        |
| F6                            | Cycle through profiler info screens                            |
| F10                           | Show/hide console                                              |
| F12                           | Take screenshot                                                |

## Version scheme

`major.minor.patch`, inherited from upstream.

- Major is incremented when the release contains breaking changes, all other
  numbers are set to 0.
- Minor is incremented when the release contains new non-breaking features,
  patch is set to 0.
- Patch is incremented when the release only contains bugfixes and very
  minor/trivial features considered necessary.

The `-dev` suffix refers to the next release: `0.0.1-dev` is the development
version leading to `0.0.1`.

## Contributing

See [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md). Issues and pull requests
for Axis go to <https://github.com/neitansh/axis-engine>. Fixes that are not
specific to Axis are worth sending upstream to Luanti as well.

## License

The engine is licensed under **LGPL-2.1-or-later**; see
[COPYING.LESSER](COPYING.LESSER). Bundled media, fonts and third-party libraries
are under their own licenses — [LICENSE.txt](LICENSE.txt) lists every one of
them together with its author.

Axis holds copyright only over the code and media it actually wrote. Everything
else belongs to its original authors, and their notices are kept as they are.

## Credits

Axis exists because Luanti exists.

- **Luanti** — Copyright (C) 2010-2026 Perttu Ahola \<celeron55@gmail.com\> and
  contributors. The engine Axis is built on:
  <https://github.com/luanti-org/luanti>
- **Irrlicht / IrrlichtMt** — the rendering library the engine uses, bundled in
  [`irr/`](irr/).
- Third-party libraries in [`lib/`](lib/), each with its own license.
- Media by the authors listed in [LICENSE.txt](LICENSE.txt).
- **Axis** — maintained by [Neitansh](https://github.com/neitansh).

Configuration
=============

The engine keeps its configuration in a directory of files, one file per
subject, inside a section that says who reads it. This document describes the
layout, how a value finds its file, and what to do when adding a setting.

Layout
------

```
config/
├── shared/     engine.conf  logging.conf
├── client/     graphics.conf  audio.conf  input.conf  keybindings.conf
│               interface.conf  network.conf  session.conf  custom.conf
└── server/     server.conf  network.conf  gameplay.conf  security.conf
                performance.conf  worldgen.conf  custom.conf
```

The directory sits in the user path (`user/config/`) and can be pointed
elsewhere with `--config-dir <path>`. It is created on the first run, with every
file present and empty apart from a comment saying what belongs in it.

`config.example/` in the source tree mirrors this layout and lists every setting
with the value the engine uses when the line is absent.

Who reads what
--------------

| Process           | Reads                          | Writes                         |
| ----------------- | ------------------------------ | ------------------------------ |
| Dedicated server  | `shared/`, `server/`           | `shared/`, `server/`           |
| Client            | `shared/`, `client/`, `server/`| the file of each value's subject |

A dedicated server never opens a client file, so client settings cannot affect
it and cannot end up in its configuration. A client does read the server
section, because the server of a single player game runs inside it, but every
value it writes goes into the file of its own subject, so the two sides never
mix inside one file.

`client/session.conf` is state the client remembers between runs - last server,
window size, which tab the main menu was on. It is not meant to be edited.

How a setting finds its file
----------------------------

`builtin/settingtypes.txt` already states, for every setting, whether the
client, the server or both read it, and groups settings into categories.
`util/generate_settings_domains.py` turns that into
`src/config/settings_domain_table.h`, the table the engine binary searches at
runtime. Rules that a category cannot express - a handful of names that sit in
a grab bag category, and the settings the engine reads without listing them in
`settingtypes.txt` - live at the top of that script.

Settings the engine does not know, which is what games and mods register, are
written back into the file they were read from. New ones go to `custom.conf` of
the running side.

A setting that ends up in the wrong file still applies: every file of the
section is read. It is moved into its own file the next time the configuration
is written, so hand edits in the wrong place heal themselves.

Adding a setting
----------------

1. Add it to `src/defaultsettings.cpp` and to `builtin/settingtypes.txt` with a
   context (`client`, `server`, `common` or `world_creation`).
2. Run `util/generate_settings_domains.py`. It regenerates the domain table and
   `config.example/`, and fails if the new setting matches no rule.
3. Commit both generated artifacts. CI (`cpp_lint` / `Settings domain table`)
   fails if they are out of date.

Code
----

| File                             | Role                                        |
| -------------------------------- | ------------------------------------------- |
| `src/config/config_domains.h`    | The domains and the file each one maps to   |
| `src/config/config_manager.h`    | Loading, saving and the split across files  |
| `src/settings.h`                 | `Settings`, unchanged apart from filtered writes |

`g_config` is the manager of the running process. `Settings::updateConfigFile()`
takes an optional filter, which is what lets one `Settings` object be written
into several files without any of them collecting settings of the others.

Other configuration files
-------------------------

These are not part of this system and keep their own names and places:

- `world.mt` - per world, see [world_format.md](world_format.md)
- `game_defaults.conf` - defaults a game ships, in its own directory
  (`minetest.conf` is still read there for compatibility with existing games)
- `mod.conf`, `modpack.conf`, `texture_pack.conf` - metadata of content
- `clientmods/mods.conf` - which client-side mods to load

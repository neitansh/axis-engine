# Contributing to Axis

Axis is a fork of [Luanti](https://github.com/luanti-org/luanti). Before you
start, it is worth deciding which of the two your change belongs to:

- **Send it here** if it touches something Axis added or changed: the
  configuration directories, Bedrock model loading, first-person objects, camera
  impulses, matchmaking, `dispatch/`, `physics_lab/`, or anything else listed in
  the README.
- **Send it upstream** if it is a fix or an improvement to engine code Axis has
  not touched. It will reach far more people there, and Axis will get it when it
  merges upstream. See
  [Luanti's contributing guide](https://github.com/luanti-org/luanti/blob/master/.github/CONTRIBUTING.md).

Doing both is fine, and sometimes the right answer.

## Code

1. [Fork](https://help.github.com/articles/fork-a-repo/) this repository and
   clone your fork.

2. For anything larger than a bug fix, open an issue first and describe what you
   intend to do. Axis is small and opinionated; agreeing on the shape of a
   change before it is written saves work on both sides.

3. Write your code.
    - The C/C++ and Lua style is Luanti's, because the codebase is Luanti's:
      [C/C++](https://docs.luanti.org/for-engine-devs/code-style-guidelines/),
      [Lua](https://docs.luanti.org/for-engine-devs/lua-code-style-guidelines/).
    - New files written for Axis carry this header (`--` for Lua, `#` for shell
      and Python):

      ```
      // Axis
      // SPDX-License-Identifier: LGPL-2.1-or-later
      // Copyright (C) 2026 the Axis contributors
      ```

      Files that came from Luanti keep their `// Luanti` header and their
      copyright lines. Do not replace them, and do not add an Axis copyright
      line to a file Axis merely edited — the git history records who wrote
      what.
    - Document any change to the Lua API in `doc/lua_api.md`.
    - Do not run `updatepo.sh`, do not edit `luanti.po{,t}`, and do not
      regenerate `config.example/` or the settings domain table with
      `util/generate_settings_domains.py`, even if your change adds translatable
      strings or new settings. Those are done in one pass before a release, and
      doing them per pull request creates conflicts between contributions.

4. Commit to a branch of its own — one change per branch, not `main`.
    - Write commit messages in the present tense.
    - The first line is a compact summary: capital letter, no full stop, under
      about 70 characters.
    - Leave the second line empty and explain the change below it.

5. Open a pull request against
   [neitansh/axis-engine](https://github.com/neitansh/axis-engine), fill in the
   template, and say what the change does and why.

A pull request is merge-able when it works, fits the rest of the engine, follows
the code style, has well-designed interfaces, and keeps protocol and file format
compatibility where compatibility is required.

## Issues

1. Search the [issue tracker](https://github.com/neitansh/axis-engine/issues)
   first.
2. Check whether the problem also happens in upstream Luanti. If it does, it is
   usually better reported there — say so in your report either way, it helps
   narrow the cause immediately.
3. [Open an issue](https://github.com/neitansh/axis-engine/issues/new) and
   include what you can:
    - the tail of `debug.txt`,
    - screenshots, if it is visible,
    - what you already tried,
    - the Axis version or commit, and the game and mods you were running,
    - your platform.

Please stay available for follow-up questions — most reports are solved by the
second or third exchange, not by the first message.

## Feature requests

Welcome, with a clear explanation of the problem being solved. Axis has no
public roadmap; whether an idea fits is decided in the issue.

## Translations

Axis has no translation project of its own. The translations shipped in `po/`
and `locale/` come from Luanti, which uses
[Weblate](https://hosted.weblate.org/projects/minetest/minetest/). Contribute
translations of engine strings there — Axis picks them up when it merges
upstream.

## Security

See [SECURITY.md](SECURITY.md). Do not open a public issue for a vulnerability.

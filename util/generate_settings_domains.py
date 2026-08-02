#!/usr/bin/env python3
"""Generates the setting -> config domain table used by ConfigManager.

Every setting the engine knows lives in exactly one configuration file. The
mapping is derived from builtin/settingtypes.txt, which already states for each
setting whether the client, the server or both read it, and groups settings
into categories. This script turns that knowledge into a C++ table so the
engine can split a running configuration across files without a second, hand
maintained list going stale.

Run from the source root:

    util/generate_settings_domains.py

It rewrites src/config/settings_domain_table.h and reports settings it could
not place. Use --check to verify the committed table is up to date, which is
what CI does.
"""

import argparse
import os
import re
import sys

SOURCE = os.path.join("builtin", "settingtypes.txt")
TARGET = os.path.join("src", "config", "settings_domain_table.h")
EXAMPLE_DIR = "config.example"

# Domain -> file, kept in sync with DOMAIN_SPECS in src/config/config_domains.cpp
DOMAIN_FILES = {
    "SharedEngine": ("shared/engine.conf",
            "Engine behaviour both the client and the server rely on."),
    "SharedLogging": ("shared/logging.conf",
            "Logging, profiling and developer switches."),
    "ClientGraphics": ("client/graphics.conf",
            "Rendering: window, view distance, shaders, effects."),
    "ClientAudio": ("client/audio.conf",
            "Sound output and volumes."),
    "ClientInput": ("client/input.conf",
            "Mouse, touchscreen and gamepad behaviour."),
    "ClientKeybindings": ("client/keybindings.conf",
            "What every key does. One line per binding."),
    "ClientInterface": ("client/interface.conf",
            "Menus, HUD, chat window and fonts."),
    "ClientNetwork": ("client/network.conf",
            "How the client talks to servers."),
    "ClientSession": ("client/session.conf",
            "State the client remembers between runs, not settings to edit by hand."),
    "ClientCustom": ("client/custom.conf",
            "Client settings of mods and texture packs, unknown to the engine."),
    "ServerServer": ("server/server.conf",
            "Identity of the server: name, world, message of the day, admin."),
    "ServerNetwork": ("server/network.conf",
            "Ports, addresses and how much the server sends to a client."),
    "ServerGameplay": ("server/gameplay.conf",
            "Rules of play: damage, physics, privileges."),
    "ServerSecurity": ("server/security.conf",
            "Mod security, anticheat and what clients are allowed to do."),
    "ServerPerformance": ("server/performance.conf",
            "Load of the server: block sending, emerge threads, map storage."),
    "ServerWorldgen": ("server/worldgen.conf",
            "Map generation. Applied to worlds at creation time."),
    "ServerCustom": ("server/custom.conf",
            "Server settings of games and mods, unknown to the engine."),
}

CATEGORY_RE = re.compile(r"^\[(\**)([^\]]+)\](?:\s+\[(\w+)\])?\s*$")
SETTING_RE = re.compile(
    r"^([a-zA-Z0-9_.\-]+)\s+\(([^)]*)\)\s*(?:\[(\w+)\]\s*)?(\S+)")

# Domains, in the order they appear in the generated enum. Keep in sync with
# ConfigDomain in src/config/config_domains.h.
DOMAINS = [
    "SharedEngine",
    "SharedLogging",
    "ClientGraphics",
    "ClientAudio",
    "ClientInput",
    "ClientKeybindings",
    "ClientInterface",
    "ClientNetwork",
    "ClientSession",
    "ClientCustom",
    "ServerServer",
    "ServerNetwork",
    "ServerGameplay",
    "ServerSecurity",
    "ServerPerformance",
    "ServerWorldgen",
    "ServerCustom",
]

# Settings whose category says less than their name does. Checked first.
BY_NAME = {
    # Category "Miscellaneous" is a grab bag; place its members by hand.
    "clickable_chat_weblinks": "ClientInterface",
    "display_density_factor": "ClientGraphics",
    "enable_console": "SharedEngine",
    "enable_remote_media_server": "ClientNetwork",
    "serverlist_file": "ClientNetwork",
    "ignore_world_load_errors": "ServerServer",
    "map-dir": "ServerServer",
    "max_clearobjects_extra_loaded_blocks": "ServerPerformance",
    "sqlite_synchronous": "ServerPerformance",
    "map_compression_level_disk": "ServerPerformance",
    # Filed under "Developer Options" but plainly server features.
    "enable_mod_channels": "ServerServer",
    # Read by both sides, despite sitting in a client category.
    "enable_all_mods": "SharedEngine",
    "serverlist_url": "SharedEngine",
    "enable_client_modding": "SharedEngine",
    "enable_sscsm": "SharedEngine",
    "main_menu_script": "ClientInterface",
    "enable_split_login_register": "ClientInterface",
    "enable_local_map_saving": "ClientNetwork",
    "language": "SharedEngine",
    # State the main menu remembers between runs. Not listed in
    # settingtypes.txt, as none of it is meant to be edited by hand.
    "mainmenu_last_selected_world": "ClientSession",
    "maintab_LAST": "ClientSession",
    "menu_last_game": "ClientSession",
    "world_config_selected_mod": "ClientSession",
    "enable_server": "ClientSession",
    "selected_serverlist_file": "ClientSession",
    # Not listed in settingtypes.txt at all.
    "curl_verify_cert": "SharedEngine",
    "debug_platform_ride": "SharedLogging",
    "disable_anticheat": "ServerSecurity",
    "dpi_change_notifier": "ClientGraphics",
    "enable_touch": "ClientInput",
    "main_menu_path": "ClientInterface",
    "opaque_water": "ClientGraphics",
    "touch_layout": "ClientInput",
    "touch_use_crosshair": "ClientInput",
}

# Name prefixes, checked after the exact names above.
BY_PREFIX = [
    ("keymap_", "ClientKeybindings"),
    ("secure.", "ServerSecurity"),
    ("instrument.", "SharedLogging"),
    ("gui_color_", "ClientInterface"),
    ("profiler_", "SharedLogging"),
]

# Category path -> domain, checked after names and prefixes. A rule matches
# when its path is a prefix of the setting's category path. Longer paths win,
# and a rule may narrow the match down to a single context.
BY_CATEGORY = [
    (("Controls", "Actions and Keybindings"), None, "ClientKeybindings"),
    (("Controls",), None, "ClientInput"),
    (("Graphics and Audio", "Graphics"), None, "ClientGraphics"),
    (("Graphics and Audio", "Effects"), None, "ClientGraphics"),
    (("Graphics and Audio", "Audio"), None, "ClientAudio"),
    (("Graphics and Audio", "User Interfaces"), None, "ClientInterface"),
    (("Client and Server", "Client"), None, "ClientNetwork"),
    (("Client and Server", "Server", "Networking"), None, "ServerNetwork"),
    (("Client and Server", "Server"), None, "ServerServer"),
    (("Client and Server", "Server Gameplay"), None, "ServerGameplay"),
    (("Client and Server", "Server Security"), None, "ServerSecurity"),
    (("Advanced", "Advanced", "Graphics"), None, "ClientGraphics"),
    (("Advanced", "Advanced", "Lighting"), None, "ClientGraphics"),
    (("Advanced", "Advanced", "Font"), None, "ClientInterface"),
    (("Advanced", "Advanced", "Sound"), None, "ClientAudio"),
    (("Advanced", "Advanced", "Networking"), "client", "ClientNetwork"),
    (("Advanced", "Advanced", "Networking"), "server", "ServerNetwork"),
    (("Advanced", "Advanced", "Networking"), "common", "SharedEngine"),
    (("Advanced", "Advanced", "cURL"), None, "SharedEngine"),
    (("Advanced", "Advanced", "Server/Env Performance"), None, "ServerPerformance"),
    (("Advanced", "Advanced", "Server"), None, "ServerServer"),
    (("Advanced", "Advanced", "Mapgen"), "world_creation", "ServerWorldgen"),
    (("Advanced", "Advanced", "Mapgen"), "server", "ServerPerformance"),
    (("Advanced", "Advanced", "Client Debugging"), None, "SharedLogging"),
    (("Advanced", "Developer Options", "Mod Security"), None, "ServerSecurity"),
    (("Advanced", "Developer Options"), None, "SharedLogging"),
    (("Advanced", "Hide: Temporary Settings"), None, "ClientSession"),
    (("Mapgen",), "world_creation", "ServerWorldgen"),
    (("Mapgen",), "server", "ServerPerformance"),
]


class Setting:
    def __init__(self, name, context, category, readable, type_, args, comment):
        self.name = name
        self.context = context
        self.category = category
        self.readable = readable
        self.type = type_
        self.args = args
        self.comment = comment


def parse_settingtypes(path):
    """Yields a Setting for every entry in the file."""
    stack = []
    comment = []

    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")

            match = CATEGORY_RE.match(line)
            if match:
                stars, name, context = match.groups()
                level = len(stars)
                stack = [entry for entry in stack if entry[0] < level]
                inherited = context or (stack[-1][2] if stack else None)
                stack.append((level, name, inherited))
                comment = []
                continue

            if line.startswith("#"):
                # Comments directly above a setting document it
                comment.append(line.lstrip("#").strip())
                continue

            if not line.strip():
                comment = []
                continue

            match = SETTING_RE.match(line)
            if match:
                name, readable, context, type_ = match.groups()
                args = line[match.end():].strip()
                yield Setting(name,
                        context or (stack[-1][2] if stack else None),
                        tuple(entry[1] for entry in stack),
                        readable, type_, args, comment)
            comment = []


def domain_of(name, context, category):
    if name in BY_NAME:
        return BY_NAME[name]

    for prefix, domain in BY_PREFIX:
        if name.startswith(prefix):
            return domain

    best = None
    for path, wanted_context, domain in BY_CATEGORY:
        if category[:len(path)] != path:
            continue
        if wanted_context and wanted_context != context:
            continue
        if best is None or len(path) > best[0]:
            best = (len(path), domain)

    return best[1] if best else None


def build_table(source):
    table = {}
    unplaced = []
    settings = []

    for setting in parse_settingtypes(source):
        domain = domain_of(setting.name, setting.context, setting.category)
        if domain is None:
            unplaced.append((setting.name, setting.context,
                    " > ".join(setting.category)))
            continue
        table[setting.name] = domain
        settings.append((domain, setting))

    # Names the engine reads without ever listing them in settingtypes.txt.
    for name, domain in BY_NAME.items():
        table.setdefault(name, domain)

    return table, unplaced, settings


def render(table):
    lines = [
        "// Luanti",
        "// SPDX-License-Identifier: LGPL-2.1-or-later",
        "",
        "// GENERATED FILE, DO NOT EDIT.",
        "// Produced by util/generate_settings_domains.py from",
        "// builtin/settingtypes.txt. Run that script after adding a setting.",
        "",
        "#pragma once",
        "",
        "#include \"config_domains.h\"",
        "",
        "namespace {",
        "",
        "// Sorted by name so the lookup can binary search.",
        "constexpr SettingDomainEntry SETTING_DOMAIN_TABLE[] = {",
    ]

    for name in sorted(table):
        lines.append("\t{\"%s\", ConfigDomain::%s}," % (name, table[name]))

    lines += [
        "};",
        "",
        "} // namespace",
        "",
    ]

    return "\n".join(lines)


def render_example(domain, settings):
    """One example file: every setting of a domain, commented out."""
    path, summary = DOMAIN_FILES[domain]

    lines = ["# %s" % summary, "#"]

    if settings:
        lines += [
            "# Every setting of this file is listed below with the value the engine",
            "# uses when the line is absent. Remove the leading # to change one.",
            "",
        ]
    else:
        lines += [
            "# The engine writes nothing here on its own. Settings of games and",
            "# mods end up in this file.",
            "",
        ]

    # Types whose default is a single token, the rest of the line being the
    # range or the list of allowed values
    BOUNDED = ("int", "float", "enum", "flags", "bool", "key")

    last_category = None
    for setting in settings:
        if setting.category != last_category:
            lines += ["#", "# %s" % " / ".join(setting.category), "#", ""]
            last_category = setting.category

        for comment in setting.comment:
            lines.append(("# " + comment).rstrip())

        default = setting.args
        if setting.type in BOUNDED:
            default = setting.args.split(" ")[0] if setting.args else ""

        lines.append(("# %s = %s" % (setting.name, default)).rstrip())
        lines.append("")

    return path, "\n".join(lines)


def write_examples(settings):
    by_domain = {}
    for domain, setting in settings:
        by_domain.setdefault(domain, []).append(setting)

    written = []
    for domain in DOMAINS:
        path, text = render_example(domain, by_domain.get(domain, []))
        full = os.path.join(EXAMPLE_DIR, *path.split("/"))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(text)
        written.append((full, len(by_domain.get(domain, []))))

    return written


def check_domains_match_engine():
    """The engine has its own copy of the file names; they must agree."""
    source = os.path.join("src", "config", "config_domains.cpp")
    with open(source, encoding="utf-8") as fh:
        found = dict(re.findall(
                r'ConfigDomain::(\w+), ConfigSection::\w+,\s*"([^"]+)"', fh.read()))

    problems = []
    for domain in DOMAINS:
        expected = DOMAIN_FILES[domain][0]
        if found.get(domain) != expected:
            problems.append("  %s: %s says %r, this script says %r"
                    % (domain, source, found.get(domain), expected))

    for domain in sorted(set(found) - set(DOMAINS)):
        problems.append("  %s: in %s but not in this script" % (domain, source))

    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
            help="fail if the committed table differs from the generated one")
    args = parser.parse_args()

    if not os.path.isfile(SOURCE):
        sys.exit("Run this from the source root: %s not found" % SOURCE)

    problems = check_domains_match_engine()
    if problems:
        print("The domains of the engine and of this script differ:", file=sys.stderr)
        print("\n".join(problems), file=sys.stderr)
        sys.exit(1)

    table, unplaced, settings = build_table(SOURCE)

    if unplaced:
        print("No domain rule matches these settings:", file=sys.stderr)
        for name, context, category in unplaced:
            print("  %s [%s] in %s" % (name, context, category), file=sys.stderr)
        sys.exit(1)

    generated = render(table)

    if args.check:
        with open(TARGET, encoding="utf-8") as fh:
            if fh.read() != generated:
                sys.exit("%s is out of date, run util/generate_settings_domains.py"
                        % TARGET)
        print("%s is up to date (%d settings)" % (TARGET, len(table)))
        return

    os.makedirs(os.path.dirname(TARGET), exist_ok=True)
    with open(TARGET, "w", encoding="utf-8") as fh:
        fh.write(generated)

    counts = {}
    for domain in table.values():
        counts[domain] = counts.get(domain, 0) + 1
    print("Wrote %s with %d settings:" % (TARGET, len(table)))
    for domain in DOMAINS:
        if domain in counts:
            print("  %-20s %d" % (domain, counts[domain]))

    print("\nWrote example configuration:")
    for path, count in write_examples(settings):
        print("  %-40s %d" % (path, count))


if __name__ == "__main__":
    main()

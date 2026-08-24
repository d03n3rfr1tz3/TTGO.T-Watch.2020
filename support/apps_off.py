Import("env")

import re

# Excluded from this fork's builds. Files stay in place for upstream merges.
APPS_OFF = [
    "app/corona_app_detector",
    "app/powermeter",
    "app/sailing",
]

# Apps that need certain hardware support.
NEEDS_ACCEL = [
    "app/games/pong",
    "app/tiltmouse",
]
NEEDS_IR = [
    "app/IRController",
]
NEEDS_MIC = [
    "app/analyzer",
    "app/soundmeter",
]

# Boards with certain available or missing hardware support.
BOARDS_WITHOUT_ACCEL = [
    "M5PAPER",
    "M5CORE2",
    "WT32_SC01",
]
BOARDS_WITH_IR = [
    "LILYGO_WATCH_2020_V1",
    "LILYGO_WATCH_2020_V2",
    "LILYGO_WATCH_2020_V3",
]
BOARDS_WITH_MIC = [
    "LILYGO_WATCH_2020_V3",
]

build_flags = env.GetProjectOption("build_flags", [])
if not isinstance(build_flags, str):
    build_flags = " ".join(build_flags)

def has_board(boards):
    return any(re.search(r"-D\s*%s\b" % board, build_flags) for board in boards)

apps_off = list(APPS_OFF)
if has_board(BOARDS_WITHOUT_ACCEL):
    apps_off += NEEDS_ACCEL
if not has_board(BOARDS_WITH_IR):
    apps_off += NEEDS_IR
if not has_board(BOARDS_WITH_MIC):
    apps_off += NEEDS_MIC

src_filter = env.get("SRC_FILTER") or []
if isinstance(src_filter, str):
    src_filter = [src_filter]

env.Replace(SRC_FILTER = list(src_filter) + ["-<%s>" % app for app in apps_off])

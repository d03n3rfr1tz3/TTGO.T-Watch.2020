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
    "app/games/breakout",
    "app/games/pong",
    "app/tiltmouse",
]
NEEDS_STEPCOUNTER = [
    "app/activity",
]
NEEDS_IR = [
    "app/IRController",
]
NEEDS_MIC = [
    "app/analyzer",
    "app/assist",
    "app/soundmeter",
    "app/voicerec",
]

# Apps that are neither a watch function, nor the watch's own sensors, nor its setup.
# Kept complete on its own, so changing another list cannot let anything back in here.
NOT_ESSENTIAL = [
    "app/analyzer",
    "app/assist",
    "app/astro",
    "app/corona_app_detector",
    "app/FindPhone",
    "app/games",
    "app/IRController",
    "app/kodi_remote",
    "app/mqtt_control",
    "app/mqtt_player",
    "app/NetTools",
    "app/osmand",
    "app/osmmap",
    "app/ping",
    "app/powermeter",
    "app/printer3d",
    "app/sailing",
    "app/soundmeter",
    "app/tiltmouse",
    "app/voicerec",
    "app/weather_station",
    "app/wifimon",
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
# Boards on twatch2021_4MB.csv: a 3.80 MB app slot with a single OTA image, no room for extras.
BOARDS_SMALL_FLASH = [
    "LILYGO_WATCH_2021",
    "WT32_SC01",
]

build_flags = env.GetProjectOption("build_flags", [])
if not isinstance(build_flags, str):
    build_flags = " ".join(build_flags)

def has_board(boards):
    return any(re.search(r"-D\s*%s\b" % board, build_flags) for board in boards)

apps_off = list(APPS_OFF)
if has_board(BOARDS_WITHOUT_ACCEL):
    apps_off += NEEDS_ACCEL
    apps_off += NEEDS_STEPCOUNTER
if not has_board(BOARDS_WITH_IR):
    apps_off += NEEDS_IR
if not has_board(BOARDS_WITH_MIC):
    apps_off += NEEDS_MIC
if has_board(BOARDS_SMALL_FLASH):
    apps_off += NOT_ESSENTIAL

src_filter = env.get("SRC_FILTER") or []
if isinstance(src_filter, str):
    src_filter = [src_filter]

env.Replace(SRC_FILTER = list(src_filter) + ["-<%s>" % app for app in apps_off])

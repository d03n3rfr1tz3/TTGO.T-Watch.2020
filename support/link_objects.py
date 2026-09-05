Import("env")

import os

# Windows has two command line limits and this project exceeds both when linking. CreateProcess caps
# at 32k, which a response file cannot fix because every stage expands it again. A linker script does
# fix it. The remaining line still has to pass cmd.exe limit at 8k.

def link_objects(env, sources):
    objects = "\n".join('"%s"' % str(source).replace("\\", "/") for source in sources)
    path = os.path.join(env.subst("$BUILD_DIR"), "objects.ld")

    with open(path, mode="w", encoding="utf8") as handle:
        handle.write("INPUT(\n%s\n)\n" % objects)

    # quoted, the project path may contain spaces and scons splits the command line on whitespace
    return '"%s"' % path.replace("\\", "/")

linkcom = env.get("LINKCOM", "")

if os.name == "nt" and "$SOURCES" in linkcom:
    env.Replace(
        _link_objects = link_objects,
        _LINK_OBJECTS = "${_link_objects(__env__, SOURCES)}",
        LINKCOM = linkcom.replace("$SOURCES", "$_LINK_OBJECTS"),
    )

# neofetch - MyOS and the Raspberry Pi at a glance, next to a logo
#   neofetch          with colours
#   neofetch plain    without
# @version 1.0
# @description System information next to a logo
import os
import sys
import myos

PLAIN = "plain" in sys.argv[1:]
R, G, C, B, OFF = ("", "", "", "", "") if PLAIN else ("\x1b[31m", "\x1b[32m", "\x1b[36m", "\x1b[1m", "\x1b[0m")

LOGO = [
    G + "    .~~.   .~~.",
    G + "   '. \\ ' ' / .'",
    R + "    .~ .~~~..~.",
    R + "   : .~.'~'.~. :",
    R + "  ~ (   ) (   ) ~",
    R + " ( : '~'.~.'~' : )",
    R + "  ~ .~ (   ) ~. ~",
    R + "   (  : '~' :  )",
    R + "    '~ .~~~. ~'",
    R + "        '~'",
]
LOGO_W = 20


def uptime(ticks):
    s = ticks // 100
    d, h, m = s // 86400, s // 3600 % 24, s // 60 % 60
    parts = []
    if d:
        parts.append("%d day%s" % (d, "s" if d != 1 else ""))
    if h:
        parts.append("%d hour%s" % (h, "s" if h != 1 else ""))
    parts.append("%d min%s" % (m, "s" if m != 1 else ""))
    return ", ".join(parts)


def packages():
    try:
        with open("pkg.db") as f:
            return len([l for l in f.read().split("\n") if l.strip()])
    except OSError:
        return 0


def main():
    info = myos.sysinfo()
    procs = myos.procs()
    mhz = info.get("mhz", 0)
    # the Pi 4 (Cortex-A72) runs at 1500 MHz or more; Zero 2 W and 3B have Cortex-A53s
    cpu = ("Cortex-A72" if mhz >= 1500 else "Cortex-A53") + " (4)" + (" @ %d MHz" % mhz if mhz else "")
    total, free = info["mem_total_kb"], info["mem_free_kb"]
    temp = info.get("temp", 0)
    programs = len([p for p in procs if not p["shell"]])
    rows = [
        ("", B + C + "myos" + OFF + "@" + B + C + "raspberrypi" + OFF),
        ("", "-" * 16),
        ("OS", "MyOS Pi (ARM64)"),
        ("Host", "Raspberry Pi" + (" 4" if mhz >= 1500 else "")),
        ("Kernel", "MyOS (written from scratch in C)"),
        ("Uptime", uptime(info["ticks"])),
        ("Packages", "%d (pkg)" % packages()),
        ("Shell", "myos"),
        ("Terminals", "%d open" % info.get("terminals", 1)),
        ("CPU", cpu),
        ("Temperature", ("%.1f C" % temp) if temp > 0 else "unknown"),
        ("Memory", "%d MiB / %d MiB" % ((total - free) // 1024, total // 1024)),
        ("Disk", "%d files, %d KB free" % (info.get("files", len(os.listdir())), info.get("disk_free_kb", 0))),
        ("Processes", "%d (%d programs)" % (len(procs), programs)),
        ("Python", "MicroPython %s" % ".".join(str(n) for n in sys.implementation.version[:3])),
        ("", ""),
    ]
    colours = "" if PLAIN else "".join("\x1b[4%dm   " % i for i in range(8)) + OFF
    print()
    for i in range(max(len(LOGO), len(rows) + 1)):
        left = LOGO[i] if i < len(LOGO) else ""
        visible = len(left) - (len(G) if left else 0)
        line = left + OFF + " " * (LOGO_W - visible)
        if i < len(rows):
            key, value = rows[i]
            line += (B + C + key + OFF + ": " + value) if key else value
        elif i == len(rows):
            line += colours
        print(line)


main()

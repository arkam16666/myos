# htop - what MyOS is doing right now: CPU, memory, temperature, Wi-Fi, processes
#   up/down (or j/k) select a process, K kills it, s changes the sort order,
#   q quits. 'htop -a' draws with plain ASCII (for terminals without UTF-8).
# @version 2.2
# @description Process viewer: CPU, memory, programs
import sys
import time
import myos

ASCII = "-a" in sys.argv[1:] or "--ascii" in sys.argv[1:]
W = 79                                  # usable columns (the terminal's width - 1: a full
ROWS = 14                               #   last column can wrap); process lines
LEFT = 38                               # width of the meters column
WIDE = False                            # room for the START column
TTY = {0: "serial", 1: "telnet", 2: "ssh1", 3: "ssh2", 4: "ssh3"}
SORTS = ("CPU%", "MEM", "PID", "TIME+")


# ---------------------------------------------------------------- colours
def fg(n):
    return "\x1b[38;5;%dm" % n


def bg(n):
    return "\x1b[48;5;%dm" % n


OFF, BOLD, DIM = "\x1b[0m", "\x1b[1m", "\x1b[2m"
TITLE = bg(24) + fg(231) + BOLD             # white on deep blue
HEAD = bg(30) + fg(16)                      # black on teal (table header)
SEL = bg(25) + fg(231)                      # the selected process
KEY = bg(37) + fg(16)                       # key labels in the footer
LABEL = fg(81)                              # "CPU", "Mem" ...
TEXT = fg(252)
GREY = fg(244)
TRACK = fg(238)                             # the empty part of a bar
GREEN, YELLOW, ORANGE, RED, CYAN, BLUE = fg(83), fg(220), fg(208), fg(203), fg(87), fg(75)
# green -> yellow -> red across a bar
RAMP = (46, 82, 118, 154, 190, 226, 220, 214, 208, 202, 196)

if ASCII:
    FULL, PARTS, EMPTY, SPARK, DEG = "|", ("",), " ", " .:-=+*#%@", "C"
    UP, DOWN, DOT_RUN, DOT_SH, DOT_END, LINE = "^", "v", "*", "o", "x", "-"
else:
    FULL, PARTS, EMPTY = "█", ("", "▏", "▎", "▍", "▌", "▋", "▊", "▉"), "─"
    SPARK = " ▁▂▃▄▅▆▇█"
    DEG, UP, DOWN, LINE = "°C", "▲", "▼", "─"
    DOT_RUN, DOT_SH, DOT_END = "●", "○", "✕"


def visible(s):
    """length on screen (without the colour codes)"""
    n, esc = 0, False
    for c in s:
        if esc:
            esc = not ("@" <= c <= "~") or c == "["
        elif c == "\x1b":
            esc = True
        else:
            n += 1
    return n


def pad(s, width):
    return s + " " * max(0, width - visible(s))


def rpad(s, width):                         # right-aligned
    return " " * max(0, width - visible(s)) + s


def level_colour(fraction):
    return fg(RAMP[min(len(RAMP) - 1, max(0, int(fraction * (len(RAMP) - 1) + 0.5)))])


# ---------------------------------------------------------------- pieces
def bar(fraction, width):
    """a meter: coloured from green to red along its length, with 1/8 steps"""
    fraction = max(0.0, min(1.0, fraction))
    eighths = int(fraction * width * len(PARTS) + 0.5)
    full, part = eighths // len(PARTS), eighths % len(PARTS)
    out = ""
    for i in range(full):
        out += fg(RAMP[i * (len(RAMP) - 1) // max(1, width - 1)]) + FULL
    if full < width:
        if part:
            out += fg(RAMP[full * (len(RAMP) - 1) // max(1, width - 1)]) + PARTS[part]
            full += 1
        out += TRACK + EMPTY * (width - full)
    return out + OFF


def meter(name, fraction, label, width=38):
    # " CPU " + bar + label (right-aligned in 10), `width` columns in all
    return " " + LABEL + BOLD + pad(name, 4) + OFF + bar(fraction, width - 16) + rpad(label, 11)


def sparkline(values, width):
    """the last `width` values (0..1) as one line of little columns"""
    values = values[-width:]
    out = " " * (width - len(values))
    for v in values:
        i = int(max(0.0, min(1.0, v)) * (len(SPARK) - 1) + 0.5)
        out += (level_colour(v) + SPARK[i]) if i else (TRACK + SPARK[1])   # idle: a grey baseline
    return out + OFF


def clock(ticks):
    s = ticks // 100
    if s >= 86400:
        return "%dd %d:%02d:%02d" % (s // 86400, s // 3600 % 24, s // 60 % 60, s % 60)
    return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def cpu_time(ticks):
    return "%d:%02d.%02d" % (ticks // 6000, ticks // 100 % 60, ticks % 100)


def size_kb(kb):
    if kb >= 1024 * 1024:
        return "%.1fG" % (kb / 1048576)
    if kb >= 1024:
        return "%dM" % (kb // 1024) if kb >= 100 * 1024 else "%.1fM" % (kb / 1024)
    return "%dK" % kb


def wifi_text():
    try:
        w = myos.wifi_status()
    except Exception:
        return GREY + "n/a" + OFF
    state = w.get("state", -1)
    if state < 0:
        return GREY + "no chip" + OFF
    if state < 2:
        return GREY + ("off" if state == 0 else "starting") + OFF
    if state < 5:
        return YELLOW + ("not connected" if state == 2 else "connecting ...") + OFF
    rssi = w.get("rssi", -100)
    lit = 4 if rssi > -55 else 3 if rssi > -65 else 2 if rssi > -75 else 1
    if ASCII:
        bars = GREEN + "#" * lit + TRACK + "#" * (4 - lit)
    else:
        marks = "▂▄▆█"
        bars = "".join((GREEN if i < lit else TRACK) + marks[i] for i in range(4))
    return bars + OFF + " " + TEXT + w.get("ssid", "")[:14] + OFF + " " + GREY + w.get("ip", "") + OFF


# ---------------------------------------------------------------- the screen
def resize():
    """fit the terminal's real size (python 1.9+ can ask it); True if it changed"""
    global W, ROWS, LEFT, WIDE
    size = None
    try:
        size = myos.termsize()
    except Exception:                       # an older python: no termsize
        pass
    cols, rows = size if size else (80, 24)
    cols, rows = max(64, min(cols, 240)), max(16, min(rows, 120))
    old = (W, ROWS)
    W, ROWS = cols - 1, rows - 9            # 9 = title, 4 meters, history, line, header, footer
    LEFT = max(38, W // 2 - 2)
    WIDE = W >= 95
    return (W, ROWS) != old


def draw(st, selected, top, message):
    info, procs, cpu = st["info"], st["procs"], st["cpu"]
    out = []
    # title
    programs = sum(1 for p in procs if not p["shell"])
    left = " MyOS htop "
    right = "up %s " % clock(info["ticks"])
    out.append(TITLE + left + " " * (W - len(left) - len(right)) + right + OFF)
    # meters (left) and facts (right)
    total = cpu["total"]
    used = info["mem_total_kb"] - info["mem_free_kb"]
    mem_frac = used / info["mem_total_kb"] if info["mem_total_kb"] else 0
    temp = info["temp"]
    lefts = [
        meter("CPU", total, level_colour(total) + "%5.1f%%" % (total * 100) + OFF, LEFT),
        meter("Mem", mem_frac, TEXT + "%s/%s" % (size_kb(used), size_kb(info["mem_total_kb"])) + OFF, LEFT),
        meter("Tmp", (temp - 30) / 55 if temp > 0 else 0,
              (level_colour((temp - 30) / 55) + "%.1f%s" % (temp, DEG) + OFF) if temp > 0 else GREY + "n/a" + OFF,
              LEFT),
        " " + LABEL + BOLD + "Net " + OFF + GREEN + UP + OFF + TEXT + " %d/s " % st["out_rate"] + OFF
        + CYAN + DOWN + OFF + TEXT + " %d/s" % st["in_rate"] + OFF + GREY + "  (USB packets)" + OFF,
    ]
    running = sum(1 for p in procs if p["running"] and not p["shell"])
    rights = [
        LABEL + "Tasks: " + OFF + BOLD + "%d" % len(procs) + OFF + TEXT + ", %d program%s, %d running"
        % (programs, "" if programs == 1 else "s", running) + OFF,
        LABEL + "Clock: " + OFF + TEXT + (("%d MHz" % info["mhz"]) if info["mhz"] else "n/a") + OFF
        + LABEL + "   Terminals: " + OFF + TEXT + "%d" % info["terminals"] + OFF,
        LABEL + "Disk:  " + OFF + TEXT + "%s free, %d files" % (size_kb(info["disk_free_kb"]), info["files"]) + OFF,
        LABEL + "Wi-Fi: " + OFF + wifi_text(),
    ]
    for l, r in zip(lefts, rights):
        out.append(pad(l, LEFT + 2) + " " + r)
    # CPU history, one column per second
    graph_w = W - 13
    out.append(" " + LABEL + BOLD + "CPU" + OFF + GREY + " history " + OFF + sparkline(st["history"], graph_w))
    out.append(GREY + " " + LINE * (W - 2) + OFF)
    # the table
    cols = ("   PID", " TTY    ", " STATE   ", "   CPU%", "    MEM", "     TIME+") \
        + (("     START",) if WIDE else ()) + ("  COMMAND",)
    head = ""
    for name in cols:
        if name.strip() == SORTS[st["sort"]]:
            head += bg(43) + fg(16) + BOLD + name + OFF + HEAD
        else:
            head += name
    out.append(HEAD + pad(head, W) + OFF)
    shown = procs[top:top + ROWS]
    for i, p in enumerate(shown):
        pid = p["pid"]
        c = cpu.get(pid, 0.0)
        if p["shell"]:
            dot, state, colour = BLUE + DOT_SH, "shell", GREY
        elif not p["running"]:
            dot, state, colour = GREY + DOT_END, "done", GREY
        elif p["foreground"]:
            dot, state, colour = GREEN + DOT_RUN, "fg", TEXT
        else:
            dot, state, colour = YELLOW + DOT_RUN, "bg", TEXT
        cpu_txt = "%6.1f" % (c * 100)
        mem = size_kb(p["mem_kb"]) if p["mem_kb"] else "-"
        name = p["name"]
        started = clock(max(0, info["ticks"] - p.get("start_ticks", info["ticks"])))
        if top + i == selected:
            text = " %5d %-7s %s %-6s %s %6s %9s" % (pid, TTY.get(p["tty"], "?"), DOT_RUN if p["running"] else " ",
                                                    state, cpu_txt, mem, cpu_time(p["cpu_ticks"])) \
                + ((" %9s" % started) if WIDE else "") + "  " + name
            out.append(SEL + BOLD + pad(text, W) + OFF)
            continue
        cpu_col = (level_colour(min(1.0, c * 2)) if c >= 0.005 else GREY) + cpu_txt + OFF
        mem_col = (CYAN if p["mem_kb"] >= 8192 else TEXT if p["mem_kb"] else GREY) + "%6s" % mem + OFF
        cmd = (CYAN + name) if p["shell"] else (BOLD + fg(231) + name)
        out.append(colour + " %5d " % pid + GREY + "%-7s " % TTY.get(p["tty"], "?") + dot + OFF + " "
                   + colour + "%-6s" % state + OFF + " " + cpu_col + " " + mem_col + " "
                   + GREY + "%9s" % cpu_time(p["cpu_ticks"]) + OFF
                   + ((" " + GREY + "%9s" % started + OFF) if WIDE else "") + "  " + cmd + OFF)
    for _ in range(ROWS - len(shown)):
        out.append("")
    # footer
    keys = (("q", "Quit"), ("↑↓" if not ASCII else "^v", "Select"), ("K", "Kill"),
            ("s", "Sort: " + SORTS[st["sort"]]))
    foot = ""
    for k, what in keys:
        foot += KEY + BOLD + " " + k + " " + OFF + TEXT + " " + what + "  " + OFF
    if message:
        foot += YELLOW + BOLD + message + OFF
    else:
        foot += GREY + "%d of %d shown" % (len(shown), len(procs)) + OFF
    out.append(foot)
    myos.move(0, 0)
    print("\x1b[K\n".join(out) + "\x1b[K", end="")


def sort_procs(procs, cpu, how):
    if how == 0:
        key = lambda p: (p["shell"], -cpu.get(p["pid"], 0), p["pid"])
    elif how == 1:
        key = lambda p: (p["shell"], -p["mem_kb"], p["pid"])
    elif how == 2:
        key = lambda p: p["pid"]
    else:
        key = lambda p: (p["shell"], -p["cpu_ticks"], p["pid"])
    procs.sort(key=key)


def main():
    resize()
    myos.clear()
    myos.cursor(False)
    selected, top, message = 0, 0, ""
    last_info = myos.sysinfo()
    last_cpu = {p["pid"]: p["cpu_ticks"] for p in myos.procs()}
    st = {"info": last_info, "procs": [], "cpu": {"total": 0.0}, "history": [], "sort": 0,
          "in_rate": 0, "out_rate": 0}
    next_update = 0
    try:
        while True:
            now = myos.ticks()
            if now >= next_update:              # new numbers once a second
                info = myos.sysinfo()
                procs = myos.procs()
                dt = max(1, info["ticks"] - last_info["ticks"])
                idle = info["idle_ticks"] - last_info["idle_ticks"]
                cpu = {"total": max(0.0, min(1.0, 1 - idle / dt))}
                for p in procs:
                    cpu[p["pid"]] = (p["cpu_ticks"] - last_cpu.get(p["pid"], p["cpu_ticks"])) / dt
                if next_update:                 # (not on the first round: no "before" yet)
                    st["history"] = (st["history"] + [cpu["total"]])[-240:]
                    st["in_rate"] = (info["net_in"] - last_info["net_in"]) * 100 // dt
                    st["out_rate"] = (info["net_out"] - last_info["net_out"]) * 100 // dt
                last_cpu = {p["pid"]: p["cpu_ticks"] for p in procs}
                last_info = info
                sort_procs(procs, cpu, st["sort"])
                st["info"], st["procs"], st["cpu"] = info, procs, cpu
                selected = min(selected, max(0, len(procs) - 1))
                next_update = now + 100
                st["rounds"] = st.get("rounds", 0) + 1
                if st["rounds"] % 3 == 0 and resize():     # the window changed size
                    myos.clear()
                if selected >= top + ROWS:
                    top = selected - ROWS + 1
                top = max(0, min(top, selected))
                draw(st, selected, top, message)
                message = ""
            k = myos.key()
            if not k:
                time.sleep_ms(30)
                continue
            procs = st["procs"]
            if k in ("q", "Q", "esc"):
                break
            if k in ("up", "k") and selected > 0:
                selected -= 1
            elif k in ("down", "j") and selected < len(procs) - 1:
                selected += 1
            elif k in ("s", "S", ">", "<"):
                st["sort"] = (st["sort"] + 1) % len(SORTS)
                sort_procs(procs, st["cpu"], st["sort"])
                message = "sorted by " + SORTS[st["sort"]]
            elif k in ("K", "x") and procs:
                p = procs[selected]
                if p["shell"]:
                    message = "can't kill a shell (close its window instead)"
                elif myos.kill(p["pid"]):
                    message = "stopped %d (%s)" % (p["pid"], p["name"])
                else:
                    message = "couldn't stop %d" % p["pid"]
                next_update = 0
                continue
            if selected < top:
                top = selected
            elif selected >= top + ROWS:
                top = selected - ROWS + 1
            draw(st, selected, top, message)
    finally:
        print(OFF, end="")
        myos.cursor(True)
        myos.clear()


main()

# htop - what MyOS is doing right now: CPU, memory, processes
#   arrows (or j/k) select a process, K kills it, q quits
# @version 1.0
# @description Process viewer: CPU, memory, programs
import time
import myos

TTY = {0: "serial", 1: "telnet", 2: "ssh1", 3: "ssh2", 4: "ssh3"}
GREEN, RED, CYAN, YELLOW, BOLD, REV, OFF = ("\x1b[32m", "\x1b[31m", "\x1b[36m", "\x1b[33m",
                                            "\x1b[1m", "\x1b[7m", "\x1b[0m")
W = 80


def bar(fraction, width, label):
    fraction = max(0.0, min(1.0, fraction))
    n = int(fraction * width + 0.5)
    colour = GREEN if fraction < 0.6 else YELLOW if fraction < 0.85 else RED
    text = colour + "|" * n + OFF + " " * (width - n)
    return "[" + text + "] " + label


def clock(ticks):
    s = ticks // 100
    return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def cpu_time(ticks):
    return "%d:%02d.%d" % (ticks // 6000, ticks // 100 % 60, ticks // 10 % 10)


def line(text=""):
    print(text + "\x1b[K")               # (clear the rest of the line)


def draw(info, procs, cpu, selected, message):
    myos.move(0, 0)
    up = clock(info["ticks"])
    programs = sum(1 for p in procs if not p["shell"])
    line(BOLD + " MyOS htop" + OFF + "   up %s   tasks: %d (%d programs)   terminals: %d"
         % (up, len(procs), programs, info["terminals"]))
    temp = ("%.1f C" % info["temp"]) if info["temp"] > 0 else "temp n/a"
    mhz = ("%d MHz" % info["mhz"]) if info["mhz"] else "clock n/a"
    line(" CPU " + bar(cpu["total"], 30, "%5.1f%%" % (cpu["total"] * 100)) + "   " + mhz + "  " + temp)
    used = info["mem_total_kb"] - info["mem_free_kb"]
    line(" Mem " + bar(used / info["mem_total_kb"], 30, "%d/%d MB" % (used // 1024, info["mem_total_kb"] // 1024))
         + "   disk: %d files, %d KB free" % (info["files"], info["disk_free_kb"]))
    line(" Net  USB network packets in %d  out %d" % (info["net_in"], info["net_out"]))
    line()
    line(REV + "   PID TTY     STATE       CPU%  MEM   TIME     NAME" + " " * 30 + OFF)
    rows = 0
    for i, p in enumerate(procs):
        if rows >= 14:
            break
        state = "shell" if p["shell"] else ("running" if p["running"] else "finished")
        if p["foreground"]:
            state = "foreground"
        mem = ("%dM" % (p["mem_kb"] // 1024)) if p["mem_kb"] else "-"
        text = " %5d %-7s %-10s %5.1f %5s %9s  %s" % (
            p["pid"], TTY.get(p["tty"], "?"), state, cpu.get(p["pid"], 0.0) * 100, mem,
            cpu_time(p["cpu_ticks"]), p["name"])
        text = text + " " * max(0, W - 1 - len(text))
        if i == selected:
            line(CYAN + REV + text + OFF)
        elif p["shell"]:
            line(CYAN + text + OFF)
        else:
            line(text)
        rows += 1
    for _ in range(14 - rows):
        line()
    line()
    line(REV + " q" + OFF + " quit  " + REV + "up/down" + OFF + " select  " + REV + "K" + OFF
         + " kill  " + (YELLOW + message + OFF if message else "updates every second"))


def main():
    myos.clear()
    myos.cursor(False)
    selected = 0
    message = ""
    last_info = myos.sysinfo()
    last_cpu = {p["pid"]: p["cpu_ticks"] for p in myos.procs()}
    cpu = {"total": 0.0}
    next_update = 0
    procs = []
    try:
        while True:
            now = myos.ticks()
            if now >= next_update:              # new numbers once a second
                info = myos.sysinfo()
                procs = myos.procs()
                dt = max(1, info["ticks"] - last_info["ticks"])
                idle = info["idle_ticks"] - last_info["idle_ticks"]
                cpu = {"total": max(0.0, 1 - idle / dt)}
                for p in procs:
                    cpu[p["pid"]] = (p["cpu_ticks"] - last_cpu.get(p["pid"], p["cpu_ticks"])) / dt
                last_cpu = {p["pid"]: p["cpu_ticks"] for p in procs}
                last_info = info
                procs.sort(key=lambda p: (p["shell"], -cpu.get(p["pid"], 0), p["pid"]))
                selected = min(selected, max(0, len(procs) - 1))
                draw(info, procs, cpu, selected, message)
                message = ""
                next_update = now + 100
            k = myos.key()
            if not k:
                time.sleep_ms(30)
                continue
            if k in ("q", "Q", "esc"):
                break
            if k in ("up", "k") and selected > 0:
                selected -= 1
            elif k in ("down", "j") and selected < len(procs) - 1:
                selected += 1
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
            draw(last_info, procs, cpu, selected, message)
    finally:
        myos.cursor(True)
        myos.clear()


main()

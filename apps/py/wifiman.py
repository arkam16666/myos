# wifiman - Wi-Fi manager: find networks, join one, see the connection
#   up/down select, Enter joins (asks for the password), r scans again,
#   d disconnects, f forgets a saved network, q quits
# @version 1.0
# @description Wi-Fi manager: scan, join, status
import time
import myos

GREEN, RED, CYAN, YELLOW, BOLD, REV, OFF = ("\x1b[32m", "\x1b[31m", "\x1b[36m", "\x1b[33m",
                                            "\x1b[1m", "\x1b[7m", "\x1b[0m")
W = 78
ROWS = 12
SECURITY = ["open", "WEP", "WPA", "WPA2", "WPA3"]
STATES = {-1: "no Wi-Fi chip", 0: "off", 1: "starting", 2: "not connected",
          3: "connecting ...", 4: "connected, getting an address ...", 5: "online"}


def line(text=""):
    print(text + "\x1b[K")


def bars(rssi):
    n = 4 if rssi >= -55 else 3 if rssi >= -67 else 2 if rssi >= -78 else 1
    colour = GREEN if n >= 3 else YELLOW if n == 2 else RED
    return colour + "#" * n + OFF + "." * (4 - n)


def draw(st, nets, selected, message):
    myos.move(0, 0)
    line(BOLD + " MyOS Wi-Fi manager" + OFF)
    line()
    state = st["state"]
    colour = GREEN if state == 5 else YELLOW if state in (1, 3, 4) else RED if state < 0 else ""
    line(" Status:   " + colour + STATES.get(state, "?") + OFF)
    if state >= 3:
        sig = ("   signal %d dBm" % st["rssi"]) if st.get("rssi") else ""
        line(" Network:  " + st["ssid"] + sig)
    else:
        line()
    if state == 5:
        line(" Address:  %s   gateway %s" % (st["ip"], st["gateway"]))
    else:
        line()
    line(" MAC:      " + st.get("mac", "") if state >= 2 else "")
    line()
    head = "   NETWORK                           SIGNAL       CH  SECURITY"
    line(REV + head + " " * (W - len(head)) + OFF)
    top = max(0, selected - ROWS + 1)
    for i in range(top, top + ROWS):
        if i >= len(nets):
            if i == 0:
                line("   (scanning ...)" if st.get("scanning") else "   (no networks found: r scans again)")
            else:
                line()
            continue
        n = nets[i]
        mark = "*" if n["saved"] else " "
        name = n["ssid"][:32]
        text = " %s %-32s %4d %s  %3d  %s" % (mark, name, n["rssi"], "    ", n["channel"],
                                             SECURITY[n["security"]] if 0 <= n["security"] < 5 else "?")
        if st["state"] >= 3 and n["ssid"] == st["ssid"]:
            text += "  <- connected" if st["state"] == 5 else "  <- joining"
        bar = bars(n["rssi"])
        # put the signal bars into the gap (they contain colour codes, so splice them in)
        cut = 3 + 32 + 1 + 4 + 1
        if i == selected:
            line(CYAN + REV + text[:cut] + OFF + bar + CYAN + REV + text[cut + 4:] + OFF)
        else:
            line(text[:cut] + bar + text[cut + 4:])
    line()
    line(" " + REV + "Enter" + OFF + " join  " + REV + "r" + OFF + " scan  " + REV + "d" + OFF
         + " disconnect  " + REV + "f" + OFF + " forget  " + REV + "q" + OFF + " quit   * = saved")
    msg = message or st.get("message", "")
    line(" " + YELLOW + msg[:W - 2] + OFF)


def ask_password(ssid):
    myos.move(0, 22)
    print(" Password for %s (Esc cancels): \x1b[K" % ssid, end="")
    myos.cursor(True)
    text = ""
    while True:
        k = myos.getkey()
        if k == "\n":
            break
        if k == "esc":
            text = None
            break
        if k == "\x08":
            if text:
                text = text[:-1]
                print("\x08 \x08", end="")
        elif len(k) == 1 and " " <= k <= "~" and len(text) < 63:
            text += k
            print("*", end="")
    myos.cursor(False)
    myos.move(0, 22)
    print("\x1b[K", end="")
    return text


def main():
    if not hasattr(myos, "wifi_status"):
        print("This Python is too old for Wi-Fi: type  pkg upgrade")
        return
    myos.clear()
    myos.cursor(False)
    st = myos.wifi_status()
    message = ""
    if st["state"] < 0:
        draw(st, [], 0, "No Wi-Fi chip here (QEMU has none). q quits.")
        while myos.getkey() not in ("q", "Q", "esc"):
            pass
        myos.cursor(True)
        myos.clear()
        return
    draw(st, [], 0, "Starting Wi-Fi and scanning ...")
    if not myos.wifi_scan():
        message = myos.wifi_status().get("message", "") or "Wi-Fi didn't start"
    nets = []
    selected = 0
    next_update = 0
    try:
        while True:
            now = myos.ticks()
            if now >= next_update:
                st = myos.wifi_status()
                nets = myos.wifi_networks()
                nets.sort(key=lambda n: -n["rssi"])
                selected = min(selected, max(0, len(nets) - 1))
                draw(st, nets, selected, message)
                next_update = now + 50
            k = myos.key()
            if not k:
                time.sleep_ms(30)
                continue
            if k in ("q", "Q", "esc"):
                break
            if k in ("up", "k") and selected > 0:
                selected -= 1
            elif k in ("down", "j") and selected < len(nets) - 1:
                selected += 1
            elif k in ("r", "R"):
                message = "scanning ..." if myos.wifi_scan() else "can't scan now"
            elif k in ("d", "D"):
                myos.wifi_disconnect()
                message = "disconnected"
            elif k in ("f", "F") and nets:
                n = nets[selected]
                message = ("forgot " + n["ssid"]) if myos.wifi_forget(n["ssid"]) else (n["ssid"] + " is not saved")
            elif k == "\n" and nets:
                n = nets[selected]
                if n["security"] in (1, 4):
                    message = SECURITY[n["security"]] + " networks are not supported"
                else:
                    password = ""
                    if n["security"] >= 2 and not n["saved"]:
                        password = ask_password(n["ssid"])
                    if password is None:
                        message = ""
                    else:
                        ok = myos.wifi_connect(n["ssid"], password)
                        message = ("joining " + n["ssid"] + " ...") if ok else "couldn't start joining"
            else:
                continue
            next_update = 0
    finally:
        myos.cursor(True)
        myos.clear()


main()

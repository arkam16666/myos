# sl - a steam locomotive crosses the screen (for when you mistype 'ls')
#   sl        the train
#   sl -l     a little one
#   sl -F     it flies
# @version 1.0
# @description Steam locomotive (when you mistype ls)
import sys
import time
import myos

W, H = 80, 24

ENGINE = [
    "      _____                    ",
    "     |_____|      ____________ ",
    "       | |  _____|  ______  | |",
    "  _____| |_|     | |  []  | | |",
    " |  ___________  | |______| | |",
    "<|=| ========= |=|__________|_|",
    " |_|___________|_|____________|",
]
WHEELS = [
    "   (O)~~(O)~~(O)     (O)  (O)  ",
    "   (O)--(O)--(O)     (O)  (O)  ",
    "   (O)__(O)__(O)     (O)  (O)  ",
]
COACH = [
    "                         ",
    "  _____________________  ",
    " |  ___   ___   ___    | ",
    " | |   | |   | |   |   | ",
    " | |___| |___| |___|   | ",
    "=|                     | ",
    " |_____________________| ",
]
COACH_WHEELS = "    (O)  (O)   (O)  (O)  "
LITTLE = [
    "    __    ",
    " __|__|__ ",
    "|  MyOS  |",
    "|________|",
]
LITTLE_WHEELS = [" (o)  (o) ", " (O)  (O) "]
SMOKE = ["  (  )", " (@@) ", "(    )", "  ()  ", " (@)  ", "   o  "]


def frames(little):
    if little:
        return [LITTLE + [w] for w in LITTLE_WHEELS]
    out = []
    for w in WHEELS:
        rows = [ENGINE[i] + COACH[i] + COACH[i] for i in range(len(ENGINE))]
        rows.append(w + COACH_WHEELS + COACH_WHEELS)
        out.append(rows)
    return out


def draw(rows, x, top, smoke):
    for i, row in enumerate(rows):
        y = top + i
        if 0 <= y < H:
            visible = (" " * max(0, x) + row[max(0, -x):])[:W]
            myos.move(0, y)
            print(visible + " " * (W - len(visible)), end="")
    for i, (sx, sy, k) in enumerate(smoke):     # puffs drift up behind the chimney
        if 0 <= sy < H and 0 <= sx < W - 6:
            myos.move(sx, sy)
            print(SMOKE[k % len(SMOKE)], end="")


def main():
    little = "-l" in sys.argv[1:]
    fly = "-F" in sys.argv[1:]
    pics = frames(little)
    width = len(pics[0][0])
    height = len(pics[0])
    top = H - height - 3 if not fly else H - height
    myos.cursor(False)
    myos.clear()
    smoke = []
    step = 0
    try:
        for x in range(W, -width - 1, -1):
            t = top - (W - x) // 6 if fly else top
            if not little and step % 2 == 0:
                smoke.append([x + 7, t - 1, step // 2])
            for p in smoke:                     # rise and fade
                p[1] -= 1 if step % 2 == 0 else 0
            smoke = [p for p in smoke if p[1] >= max(0, t - 6)]
            if fly:
                myos.move(0, t + height)
                print(" " * W, end="")
            for y in range(max(0, t - 7), t):
                myos.move(0, y)
                print(" " * W, end="")
            draw(pics[step % len(pics)], x, t, smoke if not little else [])
            step += 1
            if t + height < 0:
                break
            time.sleep_ms(30)
    finally:
        myos.move(0, H - 1)
        myos.cursor(True)
        print()


main()

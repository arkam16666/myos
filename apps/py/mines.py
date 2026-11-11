# mines - minesweeper: open every square that has no mine
# @version 1.0
# @description Minesweeper (arrows, space opens, f flags)
import random
import time
import myos

W, H, MINES = 16, 10, 18


def neighbours(x, y):
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            nx, ny = x + dx, y + dy
            if (dx or dy) and 0 <= nx < W and 0 <= ny < H:
                yield nx, ny


class Board:
    def __init__(self):
        self.mine = [[False] * W for _ in range(H)]
        self.open = [[False] * W for _ in range(H)]
        self.flag = [[False] * W for _ in range(H)]
        self.placed = False

    def place(self, sx, sy):                # mines go anywhere but the first click
        spots = [(x, y) for y in range(H) for x in range(W)
                 if abs(x - sx) > 1 or abs(y - sy) > 1]
        for _ in range(MINES):
            i = random.randint(0, len(spots) - 1)
            x, y = spots.pop(i)
            self.mine[y][x] = True
        self.placed = True

    def count(self, x, y):
        return sum(1 for nx, ny in neighbours(x, y) if self.mine[ny][nx])

    def reveal(self, x, y):
        todo = [(x, y)]
        while todo:
            x, y = todo.pop()
            if self.open[y][x] or self.flag[y][x]:
                continue
            self.open[y][x] = True
            if self.count(x, y) == 0:
                todo.extend(neighbours(x, y))

    def cell(self, x, y, show_all=False):
        if self.flag[y][x] and not show_all:
            return "F"
        if not self.open[y][x] and not show_all:
            return "#"
        if self.mine[y][x]:
            return "*"
        n = self.count(x, y)
        return str(n) if n else " "

    def won(self):
        return all(self.open[y][x] or self.mine[y][x] for y in range(H) for x in range(W))


def draw(b, cx, cy, show_all=False, msg=""):
    myos.move(0, 0)
    flags = sum(r.count(True) for r in b.flag)
    print("MINES  %d mines, %d flagged   arrows move, space open, f flag, q quit" % (MINES, flags))
    print("   +" + "---" * W + "+")
    for y in range(H):
        row = ""
        for x in range(W):
            c = b.cell(x, y, show_all)
            row += ("[" + c + "]") if (x, y) == (cx, cy) and not show_all else (" " + c + " ")
        print("   |" + row + "|")
    print("   +" + "---" * W + "+")
    print("   " + msg + " " * 40)


def main():
    b = Board()
    cx, cy = W // 2, H // 2
    start = time.time()
    myos.clear()
    myos.cursor(False)
    try:
        while True:
            draw(b, cx, cy)
            k = myos.getkey()
            if k == "q":
                return
            if k in ("left", "a"):
                cx = max(0, cx - 1)
            elif k in ("right", "d"):
                cx = min(W - 1, cx + 1)
            elif k in ("up", "w"):
                cy = max(0, cy - 1)
            elif k in ("down", "s"):
                cy = min(H - 1, cy + 1)
            elif k == "f" and not b.open[cy][cx]:
                b.flag[cy][cx] = not b.flag[cy][cx]
            elif k in (" ", "\n") and not b.flag[cy][cx]:
                if not b.placed:
                    b.place(cx, cy)
                if b.mine[cy][cx]:
                    draw(b, cx, cy, True, "BOOM! You hit a mine.")
                    return
                b.reveal(cx, cy)
                if b.won():
                    draw(b, cx, cy, True, "You cleared it in %d seconds!" % int(time.time() - start))
                    return
    finally:
        myos.cursor(True)


main()

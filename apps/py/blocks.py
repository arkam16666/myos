# blocks - falling blocks: fill whole rows to clear them
# @version 1.0
# @description Falling blocks puzzle (arrows, space)
import json
import random
import time
import myos

W, H = 10, 20
SHAPES = [
    [(0, 0), (1, 0), (2, 0), (3, 0)],      # I
    [(0, 0), (1, 0), (0, 1), (1, 1)],      # O
    [(1, 0), (0, 1), (1, 1), (2, 1)],      # T
    [(0, 0), (0, 1), (1, 1), (2, 1)],      # J
    [(2, 0), (0, 1), (1, 1), (2, 1)],      # L
    [(1, 0), (2, 0), (0, 1), (1, 1)],      # S
    [(0, 0), (1, 0), (1, 1), (2, 1)],      # Z
]
LEFT = 10                                   # screen column of the well


def scores_get():
    try:
        with open("scores.json") as f:
            return json.loads(f.read())
    except (OSError, ValueError):
        return {}


def rotate(cells):
    out = [(-y, x) for x, y in cells]
    mx = min(x for x, _ in out)
    my = min(y for _, y in out)
    return [(x - mx, y - my) for x, y in out]


class Game:
    def __init__(self):
        self.grid = [[0] * W for _ in range(H)]
        self.score = self.lines = 0
        self.shown = [[None] * W for _ in range(H)]
        self.new_piece()

    def new_piece(self):
        self.cells = random.choice(SHAPES)
        self.px, self.py = W // 2 - 2, 0
        return self.fits(self.cells, self.px, self.py)

    def fits(self, cells, px, py):
        for x, y in cells:
            x += px
            y += py
            if x < 0 or x >= W or y >= H or (y >= 0 and self.grid[y][x]):
                return False
        return True

    def lock(self):
        for x, y in self.cells:
            self.grid[self.py + y][self.px + x] = 1
        full = [r for r in range(H) if all(self.grid[r])]
        for r in full:
            del self.grid[r]
            self.grid.insert(0, [0] * W)
        self.lines += len(full)
        self.score += (0, 100, 300, 500, 800)[len(full)]

    def draw(self):
        piece = {(self.px + x, self.py + y) for x, y in self.cells}
        for y in range(H):
            for x in range(W):
                on = self.grid[y][x] or (x, y) in piece
                if self.shown[y][x] != on:          # only redraw what changed
                    self.shown[y][x] = on
                    myos.move(LEFT + 1 + 2 * x, y + 1)
                    print("[]" if on else " .", end="")
        myos.move(LEFT + 2 * W + 6, 3)
        print("Score %d" % self.score, end="")
        myos.move(LEFT + 2 * W + 6, 4)
        print("Lines %d" % self.lines, end="")
        myos.move(0, H + 2)


def frame():
    myos.clear()
    for y in range(H):
        myos.move(LEFT, y + 1)
        print("|" + " ." * W + "|", end="")
    myos.move(LEFT, H + 1)
    print("+" + "--" * W + "+", end="")
    help_x = LEFT + 2 * W + 6
    for i, t in enumerate(("BLOCKS", "", "", "", "", "left/right move", "up      rotate",
                           "down    faster", "space   drop", "q       quit")):
        myos.move(help_x, 1 + i)
        print(t, end="")


def main():
    myos.cursor(False)
    frame()
    g = Game()
    speed = 500
    try:
        alive = True
        while alive:
            g.draw()
            end = time.ticks_ms() + max(100, speed - 40 * (g.lines // 10))
            while time.ticks_ms() < end:
                k = myos.key()
                if k == "q":
                    alive = False
                    break
                if k in ("left", "a") and g.fits(g.cells, g.px - 1, g.py):
                    g.px -= 1
                elif k in ("right", "d") and g.fits(g.cells, g.px + 1, g.py):
                    g.px += 1
                elif k in ("up", "w"):
                    r = rotate(g.cells)
                    for shift in (0, -1, 1, -2):    # nudge away from walls
                        if g.fits(r, g.px + shift, g.py):
                            g.cells, g.px = r, g.px + shift
                            break
                elif k in ("down", "s"):
                    break
                elif k == " ":
                    while g.fits(g.cells, g.px, g.py + 1):
                        g.py += 1
                    break
                else:
                    time.sleep_ms(10)
                    continue
                g.draw()
            if not alive:
                break
            if g.fits(g.cells, g.px, g.py + 1):
                g.py += 1
            else:
                g.lock()
                alive = g.new_piece()
        g.draw()
    finally:
        myos.cursor(True)
        myos.move(0, H + 3)
    print("Game over. Score %d, lines %d" % (g.score, g.lines))
    s = scores_get()
    if g.score > s.get("blocks", 0):
        s["blocks"] = g.score
        with open("scores.json", "w") as f:
            f.write(json.dumps(s))
        print("New best score!")
    else:
        print("Best:", s.get("blocks", 0))


main()

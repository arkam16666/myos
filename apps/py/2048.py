# 2048 - slide the tiles; equal tiles merge. Reach 2048!
# @version 1.0
# @description 2048 sliding tile puzzle (arrow keys)
import random
import myos


def add_tile(b):
    empty = [(r, c) for r in range(4) for c in range(4) if b[r][c] == 0]
    if empty:
        r, c = random.choice(empty)
        b[r][c] = 4 if random.randint(1, 10) == 1 else 2


def slide_row(row):
    tiles = [v for v in row if v]
    out, gained, i = [], 0, 0
    while i < len(tiles):
        if i + 1 < len(tiles) and tiles[i] == tiles[i + 1]:
            out.append(tiles[i] * 2)
            gained += tiles[i] * 2
            i += 2
        else:
            out.append(tiles[i])
            i += 1
    return out + [0] * (4 - len(out)), gained


def move(b, d):
    """d = left/right/up/down; returns (new board, points)."""
    def rows(board):
        if d == "left":
            return [r[:] for r in board]
        if d == "right":
            return [r[::-1] for r in board]
        cols = [[board[r][c] for r in range(4)] for c in range(4)]
        return cols if d == "up" else [c[::-1] for c in cols]

    def unrows(rs):
        if d == "left":
            return rs
        if d == "right":
            return [r[::-1] for r in rs]
        if d == "down":
            rs = [r[::-1] for r in rs]
        return [[rs[c][r] for c in range(4)] for r in range(4)]

    total = 0
    new = []
    for r in rows(b):
        s, g = slide_row(r)
        new.append(s)
        total += g
    return unrows(new), total


def can_move(b):
    return any(move(b, d)[0] != b for d in ("left", "right", "up", "down"))


def draw(b, score):
    myos.move(0, 0)
    print("2048   score: %-8d  arrows/WASD move, q quits\n" % score)
    line = "+------+------+------+------+"
    for r in range(4):
        print(line)
        print("|" + "|".join(("%5d " % v) if v else "      " for v in b[r]) + "|")
    print(line)


def main():
    keys = {"up": "up", "down": "down", "left": "left", "right": "right",
            "w": "up", "s": "down", "a": "left", "d": "right"}
    b = [[0] * 4 for _ in range(4)]
    add_tile(b)
    add_tile(b)
    score = 0
    won = False
    myos.clear()
    myos.cursor(False)
    try:
        while True:
            draw(b, score)
            if not won and any(2048 in r for r in b):
                won = True
                print("You made 2048! Keep going if you like.")
            if not can_move(b):
                print("No moves left. Final score:", score)
                break
            k = myos.getkey()
            if k == "q":
                break
            if k in keys:
                nb, pts = move(b, keys[k])
                if nb != b:
                    b = nb
                    score += pts
                    add_tile(b)
    finally:
        myos.cursor(True)


main()

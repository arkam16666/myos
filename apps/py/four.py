# four - connect four against the computer
# @version 1.0
# @description Connect Four vs the computer
import random

COLS, ROWS = 7, 6


def drop(b, col, piece):
    for r in range(ROWS - 1, -1, -1):
        if b[r][col] == ".":
            b[r][col] = piece
            return r
    return -1


def wins(b, p):
    for r in range(ROWS):
        for c in range(COLS):
            for dr, dc in ((0, 1), (1, 0), (1, 1), (1, -1)):
                if all(0 <= r + i * dr < ROWS and 0 <= c + i * dc < COLS and
                       b[r + i * dr][c + i * dc] == p for i in range(4)):
                    return True
    return False


def score_window(w, p):
    o = "X" if p == "O" else "O"
    if w.count(p) == 4:
        return 1000
    if w.count(p) == 3 and w.count(".") == 1:
        return 6
    if w.count(p) == 2 and w.count(".") == 2:
        return 2
    if w.count(o) == 3 and w.count(".") == 1:
        return -8
    return 0


def evaluate(b, p):
    s = sum(3 for r in range(ROWS) if b[r][COLS // 2] == p)
    for r in range(ROWS):
        for c in range(COLS):
            for dr, dc in ((0, 1), (1, 0), (1, 1), (1, -1)):
                cells = [(r + i * dr, c + i * dc) for i in range(4)]
                if all(0 <= y < ROWS and 0 <= x < COLS for y, x in cells):
                    s += score_window([b[y][x] for y, x in cells], p)
    return s


def valid(b):
    return [c for c in range(COLS) if b[0][c] == "."]


def search(b, depth, maximizing, alpha, beta):
    if wins(b, "O"):
        return 100000 + depth, None
    if wins(b, "X"):
        return -100000 - depth, None
    moves = valid(b)
    if depth == 0 or not moves:
        return evaluate(b, "O"), None
    moves.sort(key=lambda c: abs(c - COLS // 2))     # try the middle first
    best_col = moves[0]
    if maximizing:
        best = -10 ** 9
        for c in moves:
            r = drop(b, c, "O")
            v, _ = search(b, depth - 1, False, alpha, beta)
            b[r][c] = "."
            if v > best:
                best, best_col = v, c
            alpha = max(alpha, v)
            if alpha >= beta:
                break
    else:
        best = 10 ** 9
        for c in moves:
            r = drop(b, c, "X")
            v, _ = search(b, depth - 1, True, alpha, beta)
            b[r][c] = "."
            if v < best:
                best, best_col = v, c
            beta = min(beta, v)
            if alpha >= beta:
                break
    return best, best_col


def show(b):
    print()
    for row in b:
        print("  | " + " ".join(row) + " |")
    print("  +" + "--" * COLS + "-+")
    print("    " + " ".join(str(i + 1) for i in range(COLS)))


def play():
    b = [["."] * COLS for _ in range(ROWS)]
    turn = random.choice("XO")
    print("You are X. Four in a row wins.")
    while True:
        if turn == "X":
            show(b)
            t = input("Your column (1-7): ").strip()
            if not t.isdigit() or int(t) - 1 not in valid(b):
                print("Pick a column that isn't full.")
                continue
            drop(b, int(t) - 1, "X")
        else:
            print("Computer is thinking...")
            _, c = search(b, 3, True, -10 ** 9, 10 ** 9)
            drop(b, c, "O")
            print("Computer plays", c + 1)
        if wins(b, turn):
            show(b)
            print("You win!" if turn == "X" else "Computer wins.")
            return
        if not valid(b):
            show(b)
            print("Draw.")
            return
        turn = "O" if turn == "X" else "X"


while True:
    play()
    if input("Again? (y/n) ").strip().lower() != "y":
        break

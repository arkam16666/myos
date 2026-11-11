# ttt - tic-tac-toe against a computer that never loses
# @version 1.0
# @description Tic-tac-toe vs an unbeatable computer
LINES = [(0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6), (1, 4, 7), (2, 5, 8), (0, 4, 8), (2, 4, 6)]


def winner(b):
    for a, c, d in LINES:
        if b[a] != " " and b[a] == b[c] == b[d]:
            return b[a]
    return "draw" if " " not in b else None


def minimax(b, me, turn):
    w = winner(b)
    if w == me:
        return 1, None
    if w == "draw":
        return 0, None
    if w:
        return -1, None
    other = "O" if turn == "X" else "X"
    best = None
    for i in range(9):
        if b[i] == " ":
            b[i] = turn
            score, _ = minimax(b, me, other)
            b[i] = " "
            if best is None or (turn == me and score > best[0]) or (turn != me and score < best[0]):
                best = (score, i)
    return best


def show(b):
    print()
    for r in range(3):
        cells = [b[r * 3 + c] if b[r * 3 + c] != " " else str(r * 3 + c + 1) for c in range(3)]
        print("  " + " | ".join(cells))
        if r < 2:
            print("  --+---+--")
    print()


def play():
    b = [" "] * 9
    you_first = input("Do you want to go first? (y/n) ").strip().lower() != "n"
    turn = "X" if you_first else "O"
    while winner(b) is None:
        if turn == "X":
            show(b)
            t = input("Your move (1-9): ").strip()
            if not t.isdigit() or not 1 <= int(t) <= 9 or b[int(t) - 1] != " ":
                print("Pick a free square 1-9.")
                continue
            b[int(t) - 1] = "X"
        else:
            if b.count(" ") == 9:
                i = 4
            else:
                _, i = minimax(b, "O", "O")
            b[i] = "O"
            print("Computer takes", i + 1)
        turn = "O" if turn == "X" else "X"
    show(b)
    w = winner(b)
    print("It's a draw." if w == "draw" else ("You win!" if w == "X" else "Computer wins."))


while True:
    play()
    if input("Again? (y/n) ").strip().lower() != "y":
        break

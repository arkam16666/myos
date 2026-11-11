# hanoi - Towers of Hanoi: watch it solve, or play it yourself
#   hanoi            watch 5 discs being solved
#   hanoi 7          watch 7 discs
#   hanoi play       move the discs yourself (type e.g. "13" = peg 1 to peg 3)
# @version 1.0
# @description Towers of Hanoi: watch or play
import sys
import time
import myos


def draw(pegs, n, moves):
    myos.move(0, 2)
    for level in range(n, 0, -1):
        row = ""
        for p in pegs:
            if len(p) >= level:
                d = p[level - 1]
                row += " " * (n - d) + "=" * (2 * d - 1) + " " * (n - d) + "  "
            else:
                row += " " * (n - 1) + "|" + " " * (n - 1) + "  "
        print(" " + row)
    print(" " + ("-" * (2 * n - 1) + "  ") * 3)
    print(" " + "".join(str(i + 1).center(2 * n - 1) + "  " for i in range(3)))
    print("\n moves: %d   (best possible: %d)   " % (moves, 2 ** n - 1))


def solve(n, a, b, c, pegs, state):
    if n == 0:
        return
    solve(n - 1, a, c, b, pegs, state)
    pegs[c].append(pegs[a].pop())
    state[0] += 1
    draw(pegs, state[1], state[0])
    time.sleep_ms(max(30, 400 // state[1]))
    if myos.key() == "q":
        raise KeyboardInterrupt
    solve(n - 1, b, a, c, pegs, state)


def main():
    args = sys.argv[1:]
    play = bool(args) and args[0] == "play"
    n = int(args[-1]) if args and args[-1].isdigit() else (4 if play else 5)
    n = max(1, min(n, 9))
    pegs = [list(range(n, 0, -1)), [], []]
    myos.clear()
    print(" TOWERS OF HANOI - move all discs to peg 3; never a big disc on a small one")
    draw(pegs, n, 0)
    if not play:
        try:
            solve(n, 0, 1, 2, pegs, [0, n])
            print(" Solved!")
        except KeyboardInterrupt:
            print(" Stopped.")
        return
    moves = 0
    while len(pegs[2]) < n:
        t = input(" move (from to, e.g. 13; q quits): ").strip()
        if t == "q":
            return
        if len(t) != 2 or t[0] not in "123" or t[1] not in "123":
            continue
        a, b = int(t[0]) - 1, int(t[1]) - 1
        if not pegs[a] or (pegs[b] and pegs[b][-1] < pegs[a][-1]):
            print(" Not allowed.        ")
            continue
        pegs[b].append(pegs[a].pop())
        moves += 1
        myos.clear()
        print(" TOWERS OF HANOI")
        draw(pegs, n, moves)
    print(" You did it in %d moves%s" % (moves, "  - perfect!" if moves == 2 ** n - 1 else ""))


main()

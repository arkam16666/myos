# sudoku - fill the grid so every row, column and 3x3 box has 1-9
#   sudoku [easy|medium|hard]
# Arrows move, 1-9 write, 0 / space / backspace erase, c checks your
# numbers, h gives a hint, n starts a new puzzle, q quits.
# @version 1.0
# @description Sudoku puzzles (easy, medium, hard)
import random
import sys
import myos

# (puzzle, solution) pairs; every game shuffles one so it looks new
PUZZLES = {
    "easy": [
        ("860009003491302500503040910000280090930010702048960030100800079300700050070000024",
         "867159243491372568523648917715283496936415782248967135154826379382794651679531824"),
        ("056410030000700500847560902400006008000290003235000640600035470070021006019600200",
         "956412837123789564847563912491356728768294153235178649682935471574821396319647285"),
        ("000029000250048370094030000900016207027005903600970500000764102402803006006000045",
         "763129458251648379894537621945316287127485963638972514589764132412853796376291845"),
        ("000500009700920060005037042100259087000803090840070203370001008401380006208060004",
         "682514739734928165915637842163259487527843691849176253376491528451382976298765314"),
        ("024006090003490800170000300010820600046759200000013574730001408008205060060008900",
         "824136795653497812179582346517824639346759281982613574735961428498275163261348957"),
        ("780000602090480500125000084019803006073609048008120090057300200000910060000208003",
         "784591632396482571125736984519843726273659148648127395857364219432915867961278453"),
    ],
    "medium": [
        ("008000000350090060700001009081000206000130000439060015870010004002600800000004973",
         "198326547354798162726451389581947236267135498439862715873519624942673851615284973"),
        ("000020504000050079053070280046500002900100005530007000604010900000009060020700053",
         "798623514261854379453971286146598732987132645532467891674315928315289467829746153"),
        ("364501027009700060000040000090020700040070080701085300000000603030009008012800004",
         "364591827189732465275648139896324751543176982721985346458217693637459218912863574"),
        ("070000600009162300100004590904500000026070030015000000041207000000301204050900010",
         "473895621589162347162734598934518762826479135715623489341287956698351274257946813"),
        ("218000700040031000000000000032506900605000014080007060400060070063000002009713540",
         "218654739547931628396872451132546987675398214984127365451269873763485192829713546"),
        ("905010000000900030864003900400000000020605080050230600518400309690000000200109500",
         "935817462172964835864523917486791253329645781751238694518476329697352148243189576"),
    ],
    "hard": [
        ("049560200100000000000000400010000002080005000000020731093870005000206003000090100",
         "749563218128947356536182479617439582382715964954628731293871645471256893865394127"),
        ("007000040900000005600240031020000450300100000000002000001506098000800500006900002",
         "217395846943681275685247931128769453374158629569432187431526798792813564856974312"),
        ("004200100000008000105000600901300000000094036000000010000005700800470095009060200",
         "784236159692518473135749682961387524257194836348652917413925768826471395579863241"),
        ("000200000036000000008007045009080700300006000057300201900030510260090004000000000",
         "495213687736854129128967345649182753312576498857349261974638512261795834583421976"),
        ("008000030900508000000014000090600300800007000030090720080400603027000000003070250",
         "218769534974538162365214897791682345852347916436195728589421673627953481143876259"),
        ("000070010067098002090002000000000201000067000073004090100009007008000060030000480",
         "352476918467198352891532746684953271519267834273814695146389527928745163735621489"),
    ],
}
BOLD, CYAN, RED, GREEN, REV, OFF = "\x1b[1m", "\x1b[36m", "\x1b[31m", "\x1b[32m", "\x1b[7m", "\x1b[0m"
TOP, LEFT = 2, 4


def shuffled(puzzle, solution):
    """the same puzzle with digits renamed, rows/columns swapped inside their
    bands, bands swapped, maybe turned over: still one solution"""
    digits = list("123456789")
    for i in range(8, 0, -1):
        j = random.randint(0, i)
        digits[i], digits[j] = digits[j], digits[i]
    rename = {"0": "0"}
    for i, d in enumerate("123456789"):
        rename[d] = digits[i]

    def order():
        bands = [0, 1, 2]
        for i in range(2, 0, -1):
            j = random.randint(0, i)
            bands[i], bands[j] = bands[j], bands[i]
        out = []
        for b in bands:
            rows = [b * 3, b * 3 + 1, b * 3 + 2]
            for i in range(2, 0, -1):
                j = random.randint(0, i)
                rows[i], rows[j] = rows[j], rows[i]
            out += rows
        return out

    rows, cols = order(), order()
    flip = random.randint(0, 1)

    def apply(s):
        out = []
        for r in range(9):
            for c in range(9):
                rr, cc = (cols[c], rows[r]) if flip else (rows[r], cols[c])
                out.append(rename[s[rr * 9 + cc]])
        return out

    return apply(puzzle), apply(solution)


def conflicts(grid):
    """cells whose number appears twice in a row, column or box"""
    bad = set()
    groups = []
    for i in range(9):
        groups.append([i * 9 + c for c in range(9)])
        groups.append([r * 9 + i for r in range(9)])
        br, bc = i // 3 * 3, i % 3 * 3
        groups.append([(br + r) * 9 + bc + c for r in range(3) for c in range(3)])
    for g in groups:
        seen = {}
        for i in g:
            d = grid[i]
            if d != "0":
                if d in seen:
                    bad.add(i)
                    bad.add(seen[d])
                seen[d] = i
    return bad


def draw(grid, given, cur, wrong, level, message):
    bad = conflicts(grid) | wrong
    myos.move(0, 0)
    print(BOLD + "SUDOKU" + OFF + " (%s)   filled %d/81\x1b[K" % (level, sum(1 for d in grid if d != "0")))
    for r in range(9):
        if r % 3 == 0:
            myos.move(LEFT, TOP + r + r // 3)
            print("+-------+-------+-------+", end="")
        myos.move(LEFT, TOP + r + r // 3 + 1)
        line = ""
        for c in range(9):
            if c % 3 == 0:
                line += "| "
            i = r * 9 + c
            d = grid[i]
            ch = "." if d == "0" else d
            style = ""
            if given[i]:
                style = BOLD + CYAN
            elif i in bad:
                style = RED
            if i == cur:
                style += REV
            line += style + ch + OFF + " "
        print(line + "|", end="")
    myos.move(LEFT, TOP + 12)
    print("+-------+-------+-------+", end="")
    myos.move(0, TOP + 14)
    print("  arrows move  1-9 write  0 erase  c check  h hint  n new  q quit\x1b[K")
    print("  " + message + "\x1b[K")


def play(level):
    puzzle, solution = shuffled(*random.choice(PUZZLES[level]))
    grid = puzzle[:]
    given = [d != "0" for d in puzzle]
    cur = given.index(False)
    wrong = set()
    message = "Fill in the dots."
    myos.clear()
    while True:
        draw(grid, given, cur, wrong, level, message)
        if grid == solution:
            myos.move(0, TOP + 16)
            print(GREEN + BOLD + "  Solved! Well done." + OFF + "  n = new puzzle, q = quit")
            while True:
                k = myos.getkey()
                if k == "q":
                    return False
                if k == "n":
                    return True
        k = myos.getkey()
        message = ""
        wrong = set()
        r, c = divmod(cur, 9)
        if k == "up":
            cur = ((r - 1) % 9) * 9 + c
        elif k == "down":
            cur = ((r + 1) % 9) * 9 + c
        elif k == "left":
            cur = r * 9 + (c - 1) % 9
        elif k == "right":
            cur = r * 9 + (c + 1) % 9
        elif k == "q":
            return False
        elif k == "n":
            return True
        elif k in "123456789" and len(k) == 1:
            if given[cur]:
                message = "That number was given - it can't change."
            else:
                grid[cur] = k
        elif k in ("0", " ", "\x7f", "\x08", "."):
            if not given[cur]:
                grid[cur] = "0"
        elif k == "c":
            wrong = set(i for i in range(81) if grid[i] != "0" and grid[i] != solution[i])
            message = ("%d wrong (in red)." % len(wrong)) if wrong else "All your numbers are right so far."
        elif k == "h":
            empty = [i for i in range(81) if grid[i] != solution[i]]
            if empty:
                i = cur if cur in empty else random.choice(empty)
                grid[i] = solution[i]
                cur = i
                message = "Hint: row %d, column %d is %s." % (i // 9 + 1, i % 9 + 1, solution[i])


def main():
    level = "easy"
    for a in sys.argv[1:]:
        if a in PUZZLES:
            level = a
    myos.cursor(False)
    try:
        while play(level):
            pass
    finally:
        myos.cursor(True)
        myos.move(0, TOP + 18)
        print()


main()

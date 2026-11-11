# cal - calendar
#   cal 11 2026     one month
#   cal 2026        the whole year
#   cal             browse month by month (arrows; MyOS has no clock, so it
#                   starts at the month you looked at last)
# The Thai year (B.E. = A.D. + 543) is shown too.
# @version 1.0
# @description Calendar: a month, a year, or browse
import sys
import myos

MONTHS = ["January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December"]
SAVE = "cal.cfg"


def leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def days_in(m, y):
    return [31, 29 if leap(y) else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]


def weekday(y, m, d):
    """0 = Sunday (Sakamoto's method)"""
    t = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4]
    if m < 3:
        y -= 1
    return (y + y // 4 - y // 100 + y // 400 + t[m - 1] + d) % 7


def month_lines(m, y, with_year=True):
    """the month as 8 lines of 20 characters"""
    title = MONTHS[m - 1] + (" %d" % y if with_year else "")
    lines = [title.center(20), "Su Mo Tu We Th Fr Sa"]
    row = "   " * weekday(y, m, 1)
    for d in range(1, days_in(m, y) + 1):
        row += "%2d " % d
        if len(row) >= 21:
            lines.append(row.rstrip())
            row = ""
    if row:
        lines.append(row.rstrip())
    while len(lines) < 8:
        lines.append("")
    return [l + " " * (20 - len(l)) for l in lines]


def print_month(m, y):
    for l in month_lines(m, y):
        print(l.rstrip())
    print("(B.E. %d)" % (y + 543))


def print_year(y):
    print(("%d  (B.E. %d)" % (y, y + 543)).center(64).rstrip())
    print()
    for q in range(4):
        blocks = [month_lines(q * 3 + i + 1, y, False) for i in range(3)]
        for row in range(8):
            print("  ".join(b[row] for b in blocks).rstrip())


def load():
    try:
        with open(SAVE) as f:
            m, y = f.read().split()
            return int(m), int(y)
    except (OSError, ValueError):
        return 1, 2026


def save(m, y):
    try:
        with open(SAVE, "w") as f:
            f.write("%d %d" % (m, y))
    except OSError:
        pass


def browse():
    m, y = load()
    myos.cursor(False)
    try:
        while True:
            myos.clear()
            print()
            for l in month_lines(m, y):
                print("  " + l)
            print("  (B.E. %d)" % (y + 543))
            print("\n  left/right: month   up/down: year   q: quit")
            k = myos.getkey()
            if k in ("q", "\x03"):
                break
            if k in ("right", "n", " "):
                m += 1
            elif k in ("left", "p"):
                m -= 1
            elif k == "up":
                y += 1
            elif k == "down":
                y -= 1
            if m > 12:
                m, y = 1, y + 1
            if m < 1:
                m, y = 12, y - 1
            y = max(1, y)
    finally:
        myos.cursor(True)
        save(m, y)
        print()


def main():
    args = sys.argv[1:]
    try:
        nums = [int(a) for a in args]
    except ValueError:
        print("usage: cal [MONTH] [YEAR]   e.g. cal 11 2026, cal 2026")
        return
    if len(nums) == 2 and 1 <= nums[0] <= 12 and nums[1] >= 1:
        print_month(nums[0], nums[1])
        save(nums[0], nums[1])
    elif len(nums) == 1 and nums[0] > 12:
        print_year(nums[0])
    elif len(nums) == 1 and 1 <= nums[0] <= 12:
        print_month(nums[0], load()[1])
    elif not nums:
        browse()
    else:
        print("usage: cal [MONTH] [YEAR]   e.g. cal 11 2026, cal 2026")


main()

# stopwatch - big-digit stopwatch and countdown timer
#   stopwatch            count up (space = pause, l = lap, q = quit)
#   stopwatch 90         count down from 90 seconds
# @version 1.0
# @description Big-digit stopwatch and countdown timer
import sys
import myos

DIGITS = {
    "0": [" ### ", "#   #", "#   #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "  ## ", " #   ", "#####"],
    "3": ["#### ", "    #", " ### ", "    #", "#### "],
    "4": ["#   #", "#   #", "#####", "    #", "    #"],
    "5": ["#####", "#    ", "#### ", "    #", "#### "],
    "6": [" ### ", "#    ", "#### ", "#   #", " ### "],
    "7": ["#####", "    #", "   # ", "  #  ", "  #  "],
    "8": [" ### ", "#   #", " ### ", "#   #", " ### "],
    "9": [" ### ", "#   #", " ####", "    #", " ### "],
    ":": ["     ", "  #  ", "     ", "  #  ", "     "],
    ".": ["     ", "     ", "     ", "     ", "  #  "],
}


def big(text):
    return ["  ".join(DIGITS[c][row] for c in text) for row in range(5)]


def fmt(cs):                                    # hundredths of a second -> "MM:SS.d"
    cs = max(0, cs)
    return "%02d:%02d.%d" % (cs // 6000, cs // 100 % 60, cs // 10 % 10)


def main():
    countdown = int(sys.argv[1]) * 100 if len(sys.argv) > 1 and sys.argv[1].isdigit() else 0
    myos.clear()
    myos.cursor(False)
    start = myos.ticks()
    paused_at = None
    laps = []
    try:
        while True:
            now = paused_at if paused_at is not None else myos.ticks()
            elapsed = now - start
            shown = countdown - elapsed if countdown else elapsed
            myos.move(0, 1)
            print("  COUNTDOWN" if countdown else "  STOPWATCH", "   space pause/go   l lap   r reset   q quit")
            print()
            for line in big(fmt(shown)):
                print("    " + line)
            print()
            for i, lap in enumerate(laps[-8:]):
                print("    lap %d   %s" % (len(laps) - len(laps[-8:]) + i + 1, fmt(lap)))
            if countdown and shown <= 0:
                print("\n    TIME'S UP!\x07")
                myos.getkey()
                break
            k = ""
            wait_until = myos.ticks() + 10              # redraw 10 times a second
            while not k and myos.ticks() < wait_until:
                k = myos.key()
            if k == "q":
                break
            if k == " ":
                if paused_at is None:
                    paused_at = myos.ticks()
                else:
                    start += myos.ticks() - paused_at
                    paused_at = None
            elif k == "l":
                laps.append(elapsed)
            elif k == "r":
                start, laps, paused_at = myos.ticks(), [], None
                myos.clear()
            myos.move(0, 0)
    finally:
        myos.cursor(True)
        myos.clear()


main()

# banner - print text in big letters
#   banner HELLO
# @version 1.0
# @description Print text in big letters
import sys

# 5x5 letters, one string of 25 characters each (row by row)
FONT = {
    "A": " ### #   ######   ##   #", "B": "#### #   ##### #   ##### ",
    "C": " #####    #    #     ####", "D": "#### #   ##   ##   ##### ",
    "E": "######    #### #    #####", "F": "######    #### #    #    ",
    "G": " #####    #  ###   # ####", "H": "#   ##   #######   ##   #",
    "I": "#####  #    #    #  #####", "J": "    #    #    ##   # ### ",
    "K": "#   ##  # ###  #  # #   #", "L": "#    #    #    #    #####",
    "M": "#   ### ## # ##   ##   #", "N": "#   ###  ## # ##  ###   #",
    "O": " ### #   ##   ##   # ### ", "P": "#### #   ##### #    #    ",
    "Q": " ### #   ## # ##  #  ## #", "R": "#### #   ##### #  # #   #",
    "S": " #####     ###     ##### ", "T": "#####  #    #    #    #  ",
    "U": "#   ##   ##   ##   # ### ", "V": "#   ##   # # #  # #   #  ",
    "W": "#   ##   ## # ### ###   #", "X": "#   # # #   #   # # #   #",
    "Y": "#   # # #   #    #    #  ", "Z": "#####   #   #   #   #####",
    "0": " ### #  ### # ###  # ### ", "1": "  #   ##    #    #  #####",
    "2": " ### #   #  ##  #   #####", "3": "####     # ###     ##### ",
    "4": "#   ##   #####    #    #", "5": "######    ####     ##### ",
    "6": " ### #    #### #   # ### ", "7": "#####   #   #   #    #   ",
    "8": " ### #   # ### #   # ### ", "9": " ### #   # ####    # ### ",
    " ": " " * 25, "!": "  #    #    #         #  ", "?": " ### #   #  ##        #  ",
    ".": "                      #  ", "-": "          ###          ",
    ":": "       #         #       ", "<": "   #  #  #     #     #   ",
    ">": " #     #     #  #  #     ", "'": "  #    #                 ",
}


def main():
    text = " ".join(sys.argv[1:]) or input("Text: ")
    text = text.upper()
    per_line = 13                                   # 6 columns per letter, 80 wide
    for start in range(0, len(text), per_line):
        chunk = text[start:start + per_line]
        for row in range(5):
            line = ""
            for ch in chunk:
                glyph = FONT.get(ch, FONT["?"])
                glyph = (glyph + " " * 25)[:25]
                line += glyph[row * 5:row * 5 + 5] + " "
            print(line.rstrip())
        print()


main()

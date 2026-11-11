# mathquiz - 10 sums against the clock
#   mathquiz          easy       mathquiz hard
# @version 1.0
# @description Mental arithmetic quiz against the clock
import random
import sys
import time


def question(hard):
    op = random.choice("+-x/" if hard else "+-x")
    top = 100 if hard else 12
    a, b = random.randint(2, top), random.randint(2, top if op != "x" else 12)
    if op == "+":
        return "%d + %d" % (a, b), a + b
    if op == "-":
        a, b = max(a, b), min(a, b)
        return "%d - %d" % (a, b), a - b
    if op == "x":
        return "%d x %d" % (a, b), a * b
    return "%d / %d" % (a * b, b), a                # always divides evenly


def main():
    hard = len(sys.argv) > 1 and sys.argv[1] == "hard"
    print("10 questions%s. Go!\n" % (" (hard)" if hard else ""))
    right = 0
    start = time.time()
    for i in range(1, 11):
        q, answer = question(hard)
        t = input("%2d)  %s = " % (i, q)).strip()
        if t.lstrip("-").isdigit() and int(t) == answer:
            right += 1
            print("      right!")
        else:
            print("      no, it's", answer)
    secs = time.time() - start
    print("\n%d/10 right in %d seconds." % (right, secs))
    print(["Keep practising!", "Not bad!", "Great!", "Perfect!"][(right >= 5) + (right >= 8) + (right == 10)])


main()

# blackjack - get closer to 21 than the dealer without going over
# @version 1.0
# @description Blackjack card game with chips
import random

RANKS = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"]
SUITS = ["S", "H", "D", "C"]


def new_deck():
    deck = [r + s for r in RANKS for s in SUITS]
    for i in range(len(deck) - 1, 0, -1):          # shuffle
        j = random.randint(0, i)
        deck[i], deck[j] = deck[j], deck[i]
    return deck


def value(hand):
    total, aces = 0, 0
    for card in hand:
        r = card[:-1]
        if r == "A":
            aces += 1
            total += 11
        elif r in ("J", "Q", "K"):
            total += 10
        else:
            total += int(r)
    while total > 21 and aces:
        total -= 10
        aces -= 1
    return total


def show(name, hand, hide=False):
    cards = [hand[0], "??"] if hide else hand
    print("  %-7s %s  %s" % (name, " ".join(cards), "" if hide else "(%d)" % value(hand)))


def round_(chips):
    while True:
        t = input("You have %d chips. Bet (Enter = 10, q = quit): " % chips).strip()
        if t == "q":
            return None
        bet = int(t) if t.isdigit() else 10
        if 1 <= bet <= chips:
            break
        print("  Bet between 1 and", chips)
    deck = new_deck()
    you, dealer = [deck.pop(), deck.pop()], [deck.pop(), deck.pop()]
    while True:
        print()
        show("Dealer", dealer, hide=True)
        show("You", you)
        if value(you) >= 21:
            break
        a = input("(h)it or (s)tand? ").strip().lower()
        if a == "h":
            you.append(deck.pop())
        elif a == "s":
            break
    if value(you) > 21:
        print("Bust! You lose", bet)
        return chips - bet
    while value(dealer) < 17:
        dealer.append(deck.pop())
    print()
    show("Dealer", dealer)
    show("You", you)
    d, y = value(dealer), value(you)
    if y == 21 and len(you) == 2 and not (d == 21 and len(dealer) == 2):
        print("Blackjack! You win", bet * 3 // 2)
        return chips + bet * 3 // 2
    if d > 21 or y > d:
        print("You win", bet)
        return chips + bet
    if y == d:
        print("Push - nobody wins.")
        return chips
    print("Dealer wins. You lose", bet)
    return chips - bet


chips = 100
while chips > 0:
    result = round_(chips)
    if result is None:
        break
    chips = result
print("You leave with %d chips." % chips if chips else "Out of chips. Better luck next time!")

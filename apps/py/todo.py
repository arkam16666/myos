# todo - a to-do list kept in todo.txt
#   todo                 show the list
#   todo add Buy milk    add a task
#   todo done 2          tick off task 2
#   todo undo 2          untick it
#   todo rm 2            delete task 2
#   todo clear           delete all ticked tasks
# @version 1.0
# @description To-do list saved on the disk
import sys

FILE = "todo.txt"


def load():
    try:
        with open(FILE) as f:
            return [l for l in f.read().split("\n") if l.strip()]
    except OSError:
        return []


def save(tasks):
    with open(FILE, "w") as f:
        f.write("\n".join(tasks))


def show(tasks):
    if not tasks:
        print("Nothing to do! Add a task:  todo add Something")
        return
    for i, t in enumerate(tasks, 1):
        mark = "[x]" if t.startswith("x ") else "[ ]"
        print("%3d %s %s" % (i, mark, t[2:]))
    left = sum(1 for t in tasks if not t.startswith("x "))
    print("%d left, %d done" % (left, len(tasks) - left))


def number(tasks, args):
    if not args or not args[0].isdigit() or not 1 <= int(args[0]) <= len(tasks):
        print("Which task? Give its number, e.g. todo done 1")
        return None
    return int(args[0]) - 1


def main():
    tasks = load()
    args = sys.argv[1:]
    cmd = args[0] if args else "list"
    if cmd == "add" and len(args) > 1:
        tasks.append("- " + " ".join(args[1:]))
    elif cmd in ("done", "undo", "rm"):
        i = number(tasks, args[1:])
        if i is None:
            return
        if cmd == "done":
            tasks[i] = "x " + tasks[i][2:]
        elif cmd == "undo":
            tasks[i] = "- " + tasks[i][2:]
        else:
            del tasks[i]
    elif cmd == "clear":
        tasks = [t for t in tasks if not t.startswith("x ")]
    elif cmd != "list":
        print("todo | todo add TEXT | todo done N | todo undo N | todo rm N | todo clear")
        return
    save(tasks)
    show(tasks)


main()

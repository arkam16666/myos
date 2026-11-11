# units - convert between units
#   units 10 km mi       units 100 f c       units 5 kg lb
#   units                (asks, and lists the units it knows)
# @version 1.0
# @description Unit converter (length, weight, temp...)
import sys

# every unit as a factor of the base unit of its group
GROUPS = {
    "length": {"mm": 0.001, "cm": 0.01, "m": 1, "km": 1000, "in": 0.0254, "ft": 0.3048,
               "yd": 0.9144, "mi": 1609.344, "wa": 2.0},          # 1 wa (Thai) = 2 m
    "weight": {"mg": 0.001, "g": 1, "kg": 1000, "t": 1e6, "oz": 28.349523, "lb": 453.59237,
               "baht": 15.2},                                      # gold baht weight
    "volume": {"ml": 0.001, "l": 1, "cup": 0.24, "gal": 3.785411, "floz": 0.0295735},
    "area":   {"m2": 1, "km2": 1e6, "ha": 1e4, "acre": 4046.856, "rai": 1600, "ngan": 400,
               "sqwa": 4},
    "speed":  {"m/s": 1, "km/h": 1 / 3.6, "mph": 0.44704, "knot": 0.514444},
    "data":   {"b": 1, "kb": 1024, "mb": 1024 ** 2, "gb": 1024 ** 3, "tb": 1024 ** 4},
    "time":   {"s": 1, "min": 60, "h": 3600, "day": 86400, "week": 604800, "year": 31557600},
}
TEMPS = ("c", "f", "k")


def to_c(v, u):
    return v if u == "c" else (v - 32) * 5 / 9 if u == "f" else v - 273.15


def from_c(v, u):
    return v if u == "c" else v * 9 / 5 + 32 if u == "f" else v + 273.15


def convert(v, a, b):
    a, b = a.lower(), b.lower()
    if a in TEMPS and b in TEMPS:
        return from_c(to_c(v, a), b)
    for units in GROUPS.values():
        if a in units and b in units:
            return v * units[a] / units[b]
    return None


def nice(x):
    if x == int(x) and abs(x) < 1e15:
        return str(int(x))
    return ("%.6g" % x)


def main():
    args = sys.argv[1:]
    if len(args) != 3:
        print("Units I know:")
        for g, units in GROUPS.items():
            print("  %-7s %s" % (g, " ".join(units)))
        print("  temp    c f k")
        line = input("Convert (e.g. 10 km mi): ").split()
        if len(line) != 3:
            return
        args = line
    try:
        v = float(args[0])
    except ValueError:
        print("The first thing must be a number.")
        return
    r = convert(v, args[1], args[2])
    if r is None:
        print("Can't convert %s to %s." % (args[1], args[2]))
    else:
        print("%s %s = %s %s" % (nice(v), args[1], nice(r), args[2]))


main()

package java.math;

public enum RoundingMode {
    UP, DOWN, CEILING, FLOOR, HALF_UP, HALF_DOWN, HALF_EVEN, UNNECESSARY;

    public static RoundingMode valueOf(int rm) {
        if (rm < 0 || rm > 7) throw new IllegalArgumentException("argument out of range");
        return values()[rm];
    }
}

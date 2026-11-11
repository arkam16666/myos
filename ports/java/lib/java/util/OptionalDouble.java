package java.util;

public final class OptionalDouble {
    private static final OptionalDouble EMPTY = new OptionalDouble(false, 0);
    private final boolean present;
    private final double value;

    private OptionalDouble(boolean present, double value) {
        this.present = present;
        this.value = value;
    }

    public static OptionalDouble empty() { return EMPTY; }

    public static OptionalDouble of(double value) { return new OptionalDouble(true, value); }

    public double getAsDouble() {
        if (!present) throw new NoSuchElementException("No value present");
        return value;
    }

    public boolean isPresent() { return present; }

    public boolean isEmpty() { return !present; }

    public double orElse(double other) { return present ? value : other; }

    public double orElseThrow() { return getAsDouble(); }

    public void ifPresent(java.util.function.DoubleConsumer action) { if (present) action.accept(value); }

    public String toString() { return present ? "OptionalDouble[" + value + "]" : "OptionalDouble.empty"; }
}

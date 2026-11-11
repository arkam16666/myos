package java.util;

public final class OptionalLong {
    private static final OptionalLong EMPTY = new OptionalLong(false, 0);
    private final boolean present;
    private final long value;

    private OptionalLong(boolean present, long value) {
        this.present = present;
        this.value = value;
    }

    public static OptionalLong empty() { return EMPTY; }

    public static OptionalLong of(long value) { return new OptionalLong(true, value); }

    public long getAsLong() {
        if (!present) throw new NoSuchElementException("No value present");
        return value;
    }

    public boolean isPresent() { return present; }

    public boolean isEmpty() { return !present; }

    public long orElse(long other) { return present ? value : other; }

    public long orElseThrow() { return getAsLong(); }

    public void ifPresent(java.util.function.LongConsumer action) { if (present) action.accept(value); }

    public String toString() { return present ? "OptionalLong[" + value + "]" : "OptionalLong.empty"; }
}

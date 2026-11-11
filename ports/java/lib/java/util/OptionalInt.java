package java.util;

public final class OptionalInt {
    private static final OptionalInt EMPTY = new OptionalInt(false, 0);
    private final boolean present;
    private final int value;

    private OptionalInt(boolean present, int value) {
        this.present = present;
        this.value = value;
    }

    public static OptionalInt empty() { return EMPTY; }

    public static OptionalInt of(int value) { return new OptionalInt(true, value); }

    public int getAsInt() {
        if (!present) throw new NoSuchElementException("No value present");
        return value;
    }

    public boolean isPresent() { return present; }

    public boolean isEmpty() { return !present; }

    public void ifPresent(java.util.function.IntConsumer action) { if (present) action.accept(value); }

    public void ifPresentOrElse(java.util.function.IntConsumer action, Runnable emptyAction) {
        if (present) action.accept(value);
        else emptyAction.run();
    }

    public int orElse(int other) { return present ? value : other; }

    public int orElseGet(java.util.function.IntSupplier supplier) { return present ? value : supplier.getAsInt(); }

    public int orElseThrow() { return getAsInt(); }

    public <X extends Throwable> int orElseThrow(java.util.function.Supplier<? extends X> s) throws X {
        if (present) return value;
        throw s.get();
    }

    public boolean equals(Object o) {
        if (!(o instanceof OptionalInt)) return false;
        OptionalInt other = (OptionalInt) o;
        return present == other.present && value == other.value;
    }

    public int hashCode() { return present ? value : 0; }

    public String toString() { return present ? "OptionalInt[" + value + "]" : "OptionalInt.empty"; }
}

package java.util.concurrent.atomic;

public class AtomicBoolean implements java.io.Serializable {
    private volatile boolean value;

    public AtomicBoolean(boolean initialValue) { value = initialValue; }

    public AtomicBoolean() {}

    public final boolean get() { return value; }

    public final void set(boolean newValue) { value = newValue; }

    public final boolean getAndSet(boolean newValue) {
        boolean old = value;
        value = newValue;
        return old;
    }

    public final boolean compareAndSet(boolean expect, boolean update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public String toString() { return Boolean.toString(value); }
}

package java.lang;

public class ThreadLocal<T> {
    private T value;
    private boolean set;

    public ThreadLocal() {}

    protected T initialValue() { return null; }

    public static <S> ThreadLocal<S> withInitial(final java.util.function.Supplier<? extends S> supplier) {
        return new ThreadLocal<S>() {
            protected S initialValue() { return supplier.get(); }
        };
    }

    public T get() {
        if (!set) {
            value = initialValue();
            set = true;
        }
        return value;
    }

    public void set(T v) {
        value = v;
        set = true;
    }

    public void remove() {
        set = false;
        value = null;
    }
}

package java.lang.reflect;

/** Only constructors without arguments (Class.getDeclaredConstructor()). */
public final class Constructor<T> {
    private final Class<T> clazz;

    public Constructor(Class<T> clazz) { this.clazz = clazz; }

    public Class<T> getDeclaringClass() { return clazz; }

    public String getName() { return clazz.getName(); }

    public int getParameterCount() { return 0; }

    public void setAccessible(boolean flag) {}

    public T newInstance(Object... initargs) throws InstantiationException, IllegalAccessException {
        if (initargs.length != 0) throw new IllegalArgumentException("wrong number of arguments: " + initargs.length + " expected: 0");
        return clazz.newInstance();
    }

    public String toString() { return "public " + clazz.getName() + "()"; }
}

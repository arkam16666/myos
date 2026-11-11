package java.lang;

public final class Class<T> implements java.io.Serializable {
    private transient long vmClass;
    private transient String name;

    private Class() {}

    public String getName() {
        if (name == null) name = getName0();
        return name;
    }

    private native String getName0();

    public native String getSimpleName();

    public String getTypeName() {
        if (isArray()) return getComponentType().getTypeName() + "[]";
        return getName();
    }

    public String getCanonicalName() { return getTypeName().replace('$', '.'); }

    public String getPackageName() {
        String n = getName();
        int i = n.lastIndexOf('.');
        return i < 0 ? "" : n.substring(0, i);
    }

    public native boolean isInstance(Object obj);

    public native boolean isAssignableFrom(Class<?> cls);

    public native boolean isArray();

    public native boolean isInterface();

    public native boolean isPrimitive();

    public native int getModifiers();

    public native Class<? super T> getSuperclass();

    public native Class<?> getComponentType();

    public boolean isEnum() { return (getModifiers() & 0x4000) != 0 && getSuperclass() == Enum.class; }

    public boolean isRecord() { return getSuperclass() == Record.class; }

    public boolean isAnonymousClass() { return getSimpleName().isEmpty(); }

    @SuppressWarnings("unchecked")
    public T[] getEnumConstants() {
        Object[] v = getEnumConstants0();
        return (T[]) v;
    }

    private native Object[] getEnumConstants0();

    public static native Class<?> forName(String className) throws ClassNotFoundException;

    public native T newInstance() throws InstantiationException, IllegalAccessException;

    static native Class<?> getPrimitiveClass(String name);

    public boolean desiredAssertionStatus() { return false; }

    @SuppressWarnings("unchecked")
    public T cast(Object obj) {
        if (obj != null && !isInstance(obj))
            throw new ClassCastException("Cannot cast " + obj.getClass().getName() + " to " + getName());
        return (T) obj;
    }

    public java.io.InputStream getResourceAsStream(String name) { return null; }

    public String toString() {
        return (isInterface() ? "interface " : (isPrimitive() ? "" : "class ")) + getName();
    }
}

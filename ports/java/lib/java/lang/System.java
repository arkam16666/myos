package java.lang;

import java.io.*;

public final class System {
    public static PrintStream out;
    public static PrintStream err;
    public static InputStream in;
    private static java.util.HashMap<String, String> props;

    static {
        out = new PrintStream(new FileOutputStream(FileDescriptor.out), true);
        err = new PrintStream(new FileOutputStream(FileDescriptor.err), true);
        in = new FileInputStream(FileDescriptor.in);
    }

    private System() {}

    public static void setOut(PrintStream o) { out = o; }

    public static void setErr(PrintStream e) { err = e; }

    public static void setIn(InputStream i) { in = i; }

    public static native void arraycopy(Object src, int srcPos, Object dest, int destPos, int length);

    public static native long currentTimeMillis();

    public static native long nanoTime();

    public static native int identityHashCode(Object x);

    public static native void exit(int status);

    public static native void gc();

    public static void runFinalization() {}

    public static String lineSeparator() { return "\n"; }

    private static native String getProperty0(String key);

    public static String getProperty(String key) {
        if (props != null && props.containsKey(key)) return props.get(key);
        return getProperty0(key);
    }

    public static String getProperty(String key, String def) {
        String v = getProperty(key);
        return v == null ? def : v;
    }

    public static String setProperty(String key, String value) {
        String old = getProperty(key);
        if (props == null) props = new java.util.HashMap<String, String>();
        props.put(key, value);
        return old;
    }

    public static String clearProperty(String key) { return setProperty(key, null); }

    public static String getenv(String name) { return null; }

    public static java.util.Map<String, String> getenv() { return new java.util.HashMap<String, String>(); }

    public static Console console() { return null; }
}

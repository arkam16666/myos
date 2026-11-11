package java.lang;

public class Runtime {
    private static final Runtime current = new Runtime();

    private Runtime() {}

    public static Runtime getRuntime() { return current; }

    public int availableProcessors() { return 1; }

    public native long freeMemory();

    public native long totalMemory();

    public native long maxMemory();

    public void gc() { System.gc(); }

    public void exit(int status) { System.exit(status); }

    public void halt(int status) { System.exit(status); }

    public void addShutdownHook(Thread hook) {}

    public boolean removeShutdownHook(Thread hook) { return false; }

    public void runFinalization() {}
}

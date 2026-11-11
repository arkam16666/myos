package java.lang;

/**
 * MyOS Java has one thread. start() runs the thread's run() right away,
 * to the end, before it returns (so join() has nothing to wait for).
 */
public class Thread implements Runnable {
    private static Thread main;
    private static int count;
    private Runnable target;
    private String name;
    private boolean daemon, started, interrupted;
    private int priority = 5;
    private long id;

    public Thread() { this((Runnable) null, (String) null); }

    public Thread(Runnable target) { this(target, null); }

    public Thread(String name) { this(null, name); }

    public Thread(Runnable target, String name) {
        this.target = target;
        this.id = ++count;
        this.name = name != null ? name : "Thread-" + (id - 1);
    }

    public Thread(ThreadGroup group, Runnable target) { this(target, null); }

    public Thread(ThreadGroup group, Runnable target, String name) { this(target, name); }

    public static Thread currentThread() {
        if (main == null) {
            main = new Thread(null, "main");
            main.started = true;
            count--;
            main.id = 1;
        }
        return main;
    }

    public void run() {
        if (target != null) target.run();
    }

    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        run();
    }

    public final void join() throws InterruptedException {}

    public final void join(long millis) throws InterruptedException {}

    public static native void sleep(long millis) throws InterruptedException;

    public static void sleep(long millis, int nanos) throws InterruptedException { sleep(millis); }

    public static void yield() {}

    public static void onSpinWait() {}

    public final String getName() { return name; }

    public final void setName(String name) { this.name = name; }

    public long getId() { return id; }

    public final long threadId() { return id; }

    public final boolean isAlive() { return false; }

    public final boolean isDaemon() { return daemon; }

    public final void setDaemon(boolean on) { daemon = on; }

    public final int getPriority() { return priority; }

    public final void setPriority(int p) { priority = p; }

    public void interrupt() { interrupted = true; }

    public boolean isInterrupted() { return interrupted; }

    public static boolean interrupted() {
        Thread t = currentThread();
        boolean r = t.interrupted;
        t.interrupted = false;
        return r;
    }

    public String toString() { return "Thread[#" + id + "," + name + "," + priority + ",main]"; }

    /* the VM calls this when main() ends with an exception */
    static void dispatchUncaughtException(Throwable e) {
        System.err.print("Exception in thread \"main\" ");
        e.printStackTrace(System.err);
        System.err.flush();
    }
}

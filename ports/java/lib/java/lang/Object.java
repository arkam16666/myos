package java.lang;

/** The root of every class (MyOS Java runtime library). */
public class Object {
    public Object() {}

    public final native Class<?> getClass();

    public native int hashCode();

    public boolean equals(Object obj) { return this == obj; }

    protected native Object clone() throws CloneNotSupportedException;

    public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }

    public final void notify() {}

    public final void notifyAll() {}

    public final void wait() throws InterruptedException {
        throw new IllegalMonitorStateException("wait() needs threads, and MyOS Java runs only one");
    }

    public final void wait(long millis) throws InterruptedException { Thread.sleep(millis); }

    public final void wait(long millis, int nanos) throws InterruptedException { Thread.sleep(millis); }

    protected void finalize() throws Throwable {}
}

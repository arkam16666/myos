package java.lang;

import java.io.PrintStream;
import java.io.PrintWriter;

public class Throwable implements java.io.Serializable {
    private String detailMessage;
    private Throwable cause;
    private transient Object backtrace;
    private StackTraceElement[] stackTrace;
    private Throwable[] suppressed;

    public Throwable() { fillInStackTrace(); }

    public Throwable(String message) {
        fillInStackTrace();
        detailMessage = message;
    }

    public Throwable(String message, Throwable cause) {
        fillInStackTrace();
        detailMessage = message;
        this.cause = cause;
    }

    public Throwable(Throwable cause) {
        fillInStackTrace();
        detailMessage = cause == null ? null : cause.toString();
        this.cause = cause;
    }

    protected Throwable(String message, Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
        if (writableStackTrace) fillInStackTrace();
        detailMessage = message;
        this.cause = cause;
    }

    public String getMessage() { return detailMessage; }

    public String getLocalizedMessage() { return getMessage(); }

    public synchronized Throwable getCause() { return cause; }

    public synchronized Throwable initCause(Throwable cause) {
        if (cause == this) throw new IllegalArgumentException("Self-causation not permitted");
        this.cause = cause;
        return this;
    }

    public String toString() {
        String s = getClass().getName();
        String message = getLocalizedMessage();
        return message != null ? s + ": " + message : s;
    }

    public synchronized Throwable fillInStackTrace() {
        fillIn0();
        stackTrace = null;
        return this;
    }

    private native void fillIn0();

    private native StackTraceElement[] buildStackTrace();

    private StackTraceElement[] ourTrace() {
        if (stackTrace == null) {
            stackTrace = backtrace == null ? new StackTraceElement[0] : buildStackTrace();
        }
        return stackTrace;
    }

    public StackTraceElement[] getStackTrace() { return ourTrace().clone(); }

    public void setStackTrace(StackTraceElement[] st) { stackTrace = st.clone(); }

    public final synchronized void addSuppressed(Throwable exception) {
        if (exception == this) throw new IllegalArgumentException("Self-suppression not permitted", exception);
        if (exception == null) throw new NullPointerException("Cannot suppress a null exception.");
        int n = suppressed == null ? 0 : suppressed.length;
        Throwable[] s = new Throwable[n + 1];
        for (int i = 0; i < n; i++) s[i] = suppressed[i];
        s[n] = exception;
        suppressed = s;
    }

    public final synchronized Throwable[] getSuppressed() {
        return suppressed == null ? new Throwable[0] : suppressed.clone();
    }

    public void printStackTrace() { printStackTrace(System.err); }

    public void printStackTrace(PrintStream s) { print(s, null); }

    public void printStackTrace(PrintWriter s) { print(null, s); }

    private static void line(PrintStream ps, PrintWriter pw, String text) {
        if (ps != null) ps.println(text);
        else pw.println(text);
    }

    private void print(PrintStream ps, PrintWriter pw) {
        line(ps, pw, String.valueOf(this));
        StackTraceElement[] trace = ourTrace();
        for (StackTraceElement e : trace) line(ps, pw, "\tat " + e);
        if (suppressed != null)
            for (Throwable t : suppressed) t.printEnclosed(ps, pw, trace, "Suppressed: ", "\t");
        Throwable c = getCause();
        if (c != null && c != this) c.printEnclosed(ps, pw, trace, "Caused by: ", "");
    }

    private void printEnclosed(PrintStream ps, PrintWriter pw, StackTraceElement[] enclosing, String caption,
                               String prefix) {
        StackTraceElement[] trace = ourTrace();
        int m = trace.length - 1, n = enclosing.length - 1;
        while (m >= 0 && n >= 0 && trace[m].equals(enclosing[n])) {
            m--;
            n--;
        }
        int common = trace.length - 1 - m;
        line(ps, pw, prefix + caption + this);
        for (int i = 0; i <= m; i++) line(ps, pw, prefix + "\tat " + trace[i]);
        if (common != 0) line(ps, pw, prefix + "\t... " + common + " more");
        if (suppressed != null)
            for (Throwable t : suppressed) t.printEnclosed(ps, pw, trace, "Suppressed: ", prefix + "\t");
        Throwable c = getCause();
        if (c != null && c != this) c.printEnclosed(ps, pw, trace, "Caused by: ", prefix);
    }
}

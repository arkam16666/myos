package java.io;

import java.util.Locale;

public class PrintStream extends OutputStream implements Appendable, Closeable {
    private OutputStream out;
    private int fd = -1;            /* the console or a file: write straight to it */
    private boolean autoFlush, error, closed;

    public PrintStream(OutputStream out) { this(out, false); }

    public PrintStream(OutputStream out, boolean autoFlush) {
        if (out == null) throw new NullPointerException("Null output stream");
        this.out = out;
        this.autoFlush = autoFlush;
        if (out instanceof FileOutputStream) fd = ((FileOutputStream) out).fd;
    }

    public PrintStream(OutputStream out, boolean autoFlush, String encoding) throws UnsupportedEncodingException {
        this(out, autoFlush);
    }

    public PrintStream(OutputStream out, boolean autoFlush, java.nio.charset.Charset cs) { this(out, autoFlush); }

    public PrintStream(String fileName) throws FileNotFoundException { this(new FileOutputStream(fileName)); }

    public PrintStream(File file) throws FileNotFoundException { this(new FileOutputStream(file)); }

    private void put(String s) {
        if (closed) {
            error = true;
            return;
        }
        try {
            if (fd >= 0) FileOutputStream.writeString0(fd, s);
            else {
                byte[] b = s.getBytes();
                out.write(b, 0, b.length);
            }
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(int b) {
        try {
            if (fd >= 0) FileOutputStream.writeBytes0(fd, new byte[] { (byte) b }, 0, 1);
            else out.write(b);
            if (b == '\n' && autoFlush) flush();
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(byte[] buf, int off, int len) {
        try {
            if (fd >= 0) FileOutputStream.writeBytes0(fd, buf, off, len);
            else out.write(buf, off, len);
            if (autoFlush) flush();
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(byte[] buf) throws IOException { write(buf, 0, buf.length); }

    public void writeBytes(byte[] buf) { write(buf, 0, buf.length); }

    public void flush() {
        try {
            if (fd >= 0) FileOutputStream.flush0(fd);
            else out.flush();
        } catch (IOException e) {
            error = true;
        }
    }

    public void close() {
        if (closed) return;
        flush();
        try {
            out.close();
        } catch (IOException e) {
            error = true;
        }
        closed = true;
    }

    public boolean checkError() {
        flush();
        return error;
    }

    protected void setError() { error = true; }

    protected void clearError() { error = false; }

    public void print(boolean b) { put(b ? "true" : "false"); }

    public void print(char c) { put(String.valueOf(c)); }

    public void print(int i) { put(String.valueOf(i)); }

    public void print(long l) { put(String.valueOf(l)); }

    public void print(float f) { put(String.valueOf(f)); }

    public void print(double d) { put(String.valueOf(d)); }

    public void print(char[] s) { put(new String(s)); }

    public void print(String s) { put(s == null ? "null" : s); }

    public void print(Object obj) { put(String.valueOf(obj)); }

    private void newLine() {
        put("\n");
        if (autoFlush && fd < 0) flush();
    }

    public void println() { newLine(); }

    public void println(boolean x) { put(x ? "true\n" : "false\n"); }

    public void println(char x) { put(String.valueOf(x).concat("\n")); }

    public void println(int x) { put(String.valueOf(x).concat("\n")); }

    public void println(long x) { put(String.valueOf(x).concat("\n")); }

    public void println(float x) { put(String.valueOf(x).concat("\n")); }

    public void println(double x) { put(String.valueOf(x).concat("\n")); }

    public void println(char[] x) { put(new String(x).concat("\n")); }

    public void println(String x) {
        put(x == null ? "null\n" : x.concat("\n"));
        if (autoFlush && fd < 0) flush();
    }

    public void println(Object x) { println(String.valueOf(x)); }

    public PrintStream printf(String format, Object... args) { return format(format, args); }

    public PrintStream printf(Locale l, String format, Object... args) { return format(format, args); }

    public PrintStream format(String format, Object... args) {
        put(new java.util.Formatter().format(format, args).toString());
        return this;
    }

    public PrintStream format(Locale l, String format, Object... args) { return format(format, args); }

    public PrintStream append(CharSequence csq) {
        print(String.valueOf(csq));
        return this;
    }

    public PrintStream append(CharSequence csq, int start, int end) {
        return append(csq == null ? "null".subSequence(start, end) : csq.subSequence(start, end));
    }

    public PrintStream append(char c) {
        print(c);
        return this;
    }
}

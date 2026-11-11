package java.io;

import java.util.Locale;

public class PrintWriter extends Writer {
    protected Writer out;
    private final boolean autoFlush;
    private boolean error;

    public PrintWriter(Writer out) { this(out, false); }

    public PrintWriter(Writer out, boolean autoFlush) {
        this.out = out;
        this.autoFlush = autoFlush;
    }

    public PrintWriter(OutputStream out) { this(out, false); }

    public PrintWriter(OutputStream out, boolean autoFlush) { this(new OutputStreamWriter(out), autoFlush); }

    public PrintWriter(String fileName) throws FileNotFoundException { this(new OutputStreamWriter(new FileOutputStream(fileName)), false); }

    public PrintWriter(File file) throws FileNotFoundException { this(new OutputStreamWriter(new FileOutputStream(file)), false); }

    public PrintWriter(String fileName, String csn) throws FileNotFoundException, UnsupportedEncodingException { this(fileName); }

    public void flush() {
        try {
            out.flush();
        } catch (IOException e) {
            error = true;
        }
    }

    public void close() {
        try {
            out.close();
        } catch (IOException e) {
            error = true;
        }
    }

    public boolean checkError() {
        flush();
        return error;
    }

    public void write(int c) {
        try {
            out.write(c);
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(char[] buf, int off, int len) {
        try {
            out.write(buf, off, len);
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(char[] buf) { write(buf, 0, buf.length); }

    public void write(String s, int off, int len) {
        try {
            out.write(s, off, len);
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(String s) { write(s, 0, s.length()); }

    public void print(boolean b) { write(b ? "true" : "false"); }

    public void print(char c) { write(c); }

    public void print(int i) { write(String.valueOf(i)); }

    public void print(long l) { write(String.valueOf(l)); }

    public void print(float f) { write(String.valueOf(f)); }

    public void print(double d) { write(String.valueOf(d)); }

    public void print(char[] s) { write(s); }

    public void print(String s) { write(String.valueOf(s)); }

    public void print(Object obj) { write(String.valueOf(obj)); }

    public void println() {
        write('\n');
        if (autoFlush) flush();
    }

    public void println(boolean x) { print(x); println(); }

    public void println(char x) { print(x); println(); }

    public void println(int x) { print(x); println(); }

    public void println(long x) { print(x); println(); }

    public void println(float x) { print(x); println(); }

    public void println(double x) { print(x); println(); }

    public void println(char[] x) { print(x); println(); }

    public void println(String x) { print(x); println(); }

    public void println(Object x) { print(String.valueOf(x)); println(); }

    public PrintWriter printf(String format, Object... args) { return format(format, args); }

    public PrintWriter printf(Locale l, String format, Object... args) { return format(format, args); }

    public PrintWriter format(String format, Object... args) {
        write(new java.util.Formatter().format(format, args).toString());
        if (autoFlush) flush();
        return this;
    }

    public PrintWriter format(Locale l, String format, Object... args) { return format(format, args); }

    public PrintWriter append(CharSequence csq) {
        write(String.valueOf(csq));
        return this;
    }

    public PrintWriter append(CharSequence csq, int start, int end) {
        if (csq == null) csq = "null";
        return append(csq.subSequence(start, end));
    }

    public PrintWriter append(char c) {
        write(c);
        return this;
    }
}

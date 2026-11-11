package java.io;

public abstract class Reader implements Closeable {
    protected Reader() {}

    public int read() throws IOException {
        char[] cb = new char[1];
        return read(cb, 0, 1) == -1 ? -1 : cb[0];
    }

    public int read(char[] cbuf) throws IOException { return read(cbuf, 0, cbuf.length); }

    public abstract int read(char[] cbuf, int off, int len) throws IOException;

    public long skip(long n) throws IOException {
        long k = 0;
        while (k < n && read() >= 0) k++;
        return k;
    }

    public boolean ready() throws IOException { return false; }

    public boolean markSupported() { return false; }

    public void mark(int readAheadLimit) throws IOException { throw new IOException("mark() not supported"); }

    public void reset() throws IOException { throw new IOException("reset() not supported"); }

    public long transferTo(Writer out) throws IOException {
        char[] buf = new char[1024];
        long t = 0;
        int n;
        while ((n = read(buf, 0, buf.length)) > 0) {
            out.write(buf, 0, n);
            t += n;
        }
        return t;
    }

    public abstract void close() throws IOException;
}

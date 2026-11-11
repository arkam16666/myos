package java.io;

public abstract class InputStream implements Closeable {
    public InputStream() {}

    public abstract int read() throws IOException;

    public int read(byte[] b) throws IOException { return read(b, 0, b.length); }

    public int read(byte[] b, int off, int len) throws IOException {
        if (len == 0) return 0;
        int c = read();
        if (c == -1) return -1;
        b[off] = (byte) c;
        int i = 1;
        for (; i < len; i++) {
            c = read();
            if (c == -1) break;
            b[off + i] = (byte) c;
        }
        return i;
    }

    public byte[] readAllBytes() throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n;
        while ((n = read(buf, 0, buf.length)) > 0) bo.write(buf, 0, n);
        return bo.toByteArray();
    }

    public long transferTo(OutputStream out) throws IOException {
        byte[] buf = new byte[4096];
        long t = 0;
        int n;
        while ((n = read(buf, 0, buf.length)) > 0) {
            out.write(buf, 0, n);
            t += n;
        }
        return t;
    }

    public long skip(long n) throws IOException {
        long k = 0;
        while (k < n && read() >= 0) k++;
        return k;
    }

    public int available() throws IOException { return 0; }

    public void close() throws IOException {}

    public boolean markSupported() { return false; }

    public void mark(int readlimit) {}

    public void reset() throws IOException { throw new IOException("mark/reset not supported"); }
}

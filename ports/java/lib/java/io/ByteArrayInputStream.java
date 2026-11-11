package java.io;

public class ByteArrayInputStream extends InputStream {
    protected byte[] buf;
    protected int pos, mark, count;

    public ByteArrayInputStream(byte[] buf) { this(buf, 0, buf.length); }

    public ByteArrayInputStream(byte[] buf, int offset, int length) {
        this.buf = buf;
        this.pos = offset;
        this.mark = offset;
        this.count = Math.min(offset + length, buf.length);
    }

    public int read() { return (pos < count) ? (buf[pos++] & 0xff) : -1; }

    public int read(byte[] b, int off, int len) {
        if (pos >= count) return -1;
        int avail = count - pos;
        if (len > avail) len = avail;
        if (len <= 0) return 0;
        System.arraycopy(buf, pos, b, off, len);
        pos += len;
        return len;
    }

    public int available() { return count - pos; }

    public boolean markSupported() { return true; }

    public void mark(int readAheadLimit) { mark = pos; }

    public void reset() { pos = mark; }
}

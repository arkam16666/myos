package java.io;

public class ByteArrayOutputStream extends OutputStream {
    protected byte[] buf;
    protected int count;

    public ByteArrayOutputStream() { this(32); }

    public ByteArrayOutputStream(int size) { buf = new byte[size]; }

    private void grow(int need) {
        if (need > buf.length) buf = java.util.Arrays.copyOf(buf, Math.max(need, buf.length * 2));
    }

    public void write(int b) {
        grow(count + 1);
        buf[count++] = (byte) b;
    }

    public void write(byte[] b, int off, int len) {
        grow(count + len);
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }

    public void writeBytes(byte[] b) { write(b, 0, b.length); }

    public void writeTo(OutputStream out) throws IOException { out.write(buf, 0, count); }

    public void reset() { count = 0; }

    public byte[] toByteArray() { return java.util.Arrays.copyOf(buf, count); }

    public int size() { return count; }

    public String toString() { return new String(buf, 0, count); }

    public String toString(String charsetName) throws UnsupportedEncodingException { return new String(buf, 0, count, charsetName); }

    public String toString(java.nio.charset.Charset cs) { return new String(buf, 0, count, cs); }

    public void close() {}
}

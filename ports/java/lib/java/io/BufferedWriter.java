package java.io;

public class BufferedWriter extends Writer {
    private final Writer out;
    private final StringBuilder buf = new StringBuilder();

    public BufferedWriter(Writer out) { this.out = out; }

    public BufferedWriter(Writer out, int size) { this.out = out; }

    public void write(char[] cbuf, int off, int len) throws IOException {
        buf.append(cbuf, off, len);
        if (buf.length() >= 8192) flushBuffer();
    }

    public void write(String s, int off, int len) throws IOException {
        buf.append(s, off, off + len);
        if (buf.length() >= 8192) flushBuffer();
    }

    public void write(int c) throws IOException {
        buf.append((char) c);
        if (buf.length() >= 8192) flushBuffer();
    }

    public void newLine() throws IOException { write('\n'); }

    private void flushBuffer() throws IOException {
        if (buf.length() > 0) {
            out.write(buf.toString());
            buf.setLength(0);
        }
    }

    public void flush() throws IOException {
        flushBuffer();
        out.flush();
    }

    public void close() throws IOException {
        flush();
        out.close();
    }
}

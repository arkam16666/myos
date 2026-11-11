package java.io;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;

    public OutputStreamWriter(OutputStream out) { this.out = out; }

    public OutputStreamWriter(OutputStream out, String charsetName) throws UnsupportedEncodingException { this(out); }

    public OutputStreamWriter(OutputStream out, java.nio.charset.Charset cs) { this(out); }

    public String getEncoding() { return "UTF8"; }

    public void write(char[] cbuf, int off, int len) throws IOException {
        byte[] b = new String(cbuf, off, len).getBytes();
        out.write(b, 0, b.length);
    }

    public void write(String str, int off, int len) throws IOException {
        byte[] b = str.substring(off, off + len).getBytes();
        out.write(b, 0, b.length);
    }

    public void flush() throws IOException { out.flush(); }

    public void close() throws IOException { out.close(); }
}

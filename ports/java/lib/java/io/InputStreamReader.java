package java.io;

public class InputStreamReader extends Reader {
    private final InputStream in;
    private final byte[] buf = new byte[1024];
    private int pos, len;
    private int pending = -1;       /* low surrogate waiting to be returned */

    public InputStreamReader(InputStream in) { this.in = in; }

    public InputStreamReader(InputStream in, String charsetName) throws UnsupportedEncodingException { this(in); }

    public InputStreamReader(InputStream in, java.nio.charset.Charset cs) { this(in); }

    public String getEncoding() { return "UTF8"; }

    private int nextByte() throws IOException {
        if (pos >= len) {
            len = in.read(buf, 0, buf.length);
            pos = 0;
            if (len <= 0) {
                len = 0;
                return -1;
            }
        }
        return buf[pos++] & 255;
    }

    public int read() throws IOException {
        if (pending >= 0) {
            int p = pending;
            pending = -1;
            return p;
        }
        int c = nextByte();
        if (c < 0x80) return c;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (more == 0) return 0xFFFD;
        int cp = c & (0x3F >> more);
        for (int k = 0; k < more; k++) {
            int b = nextByte();
            if (b < 0 || (b & 0xC0) != 0x80) return 0xFFFD;
            cp = cp << 6 | (b & 0x3F);
        }
        if (cp >= 0x10000) {
            char[] pair = Character.toChars(cp);
            pending = pair[1];
            return pair[0];
        }
        return cp;
    }

    public int read(char[] cbuf, int off, int n) throws IOException {
        if (n == 0) return 0;
        int i = 0;
        while (i < n) {
            if (i > 0 && pending < 0 && pos >= len) break;      /* don't block for more */
            int c = read();
            if (c < 0) break;
            cbuf[off + i++] = (char) c;
            if (c == '\n') break;
        }
        return i == 0 ? -1 : i;
    }

    public boolean ready() throws IOException { return pos < len || pending >= 0; }

    public void close() throws IOException { in.close(); }
}

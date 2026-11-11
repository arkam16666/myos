package java.io;

import java.util.ArrayList;

public class BufferedReader extends Reader {
    private final Reader in;
    private char[] buf = new char[1024];
    private int pos, len;
    private boolean skipLF;

    public BufferedReader(Reader in) { this.in = in; }

    public BufferedReader(Reader in, int size) { this.in = in; }

    private boolean fill() throws IOException {
        if (pos < len) return true;
        int n = in.read(buf, 0, buf.length);
        pos = 0;
        len = n > 0 ? n : 0;
        return n > 0;
    }

    public int read() throws IOException {
        for (;;) {
            if (!fill()) return -1;
            char c = buf[pos++];
            if (skipLF) {
                skipLF = false;
                if (c == '\n') continue;
            }
            return c;
        }
    }

    public int read(char[] cbuf, int off, int n) throws IOException {
        if (n == 0) return 0;
        int c = read();
        if (c < 0) return -1;
        cbuf[off] = (char) c;
        int i = 1;
        while (i < n && pos < len) cbuf[off + i++] = buf[pos++];
        return i;
    }

    public String readLine() throws IOException {
        StringBuilder sb = null;
        for (;;) {
            if (!fill()) return sb == null ? null : sb.toString();
            if (skipLF) {
                skipLF = false;
                if (buf[pos] == '\n') {
                    pos++;
                    continue;
                }
            }
            int start = pos;
            while (pos < len) {
                char c = buf[pos];
                if (c == '\n' || c == '\r') {
                    if (sb == null) sb = new StringBuilder();
                    sb.append(buf, start, pos - start);
                    pos++;
                    if (c == '\r') skipLF = true;
                    return sb.toString();
                }
                pos++;
            }
            if (sb == null) sb = new StringBuilder();
            sb.append(buf, start, pos - start);
        }
    }

    public boolean ready() throws IOException { return pos < len || in.ready(); }

    public java.util.stream.Stream<String> lines() {
        ArrayList<String> list = new ArrayList<String>();
        try {
            String l;
            while ((l = readLine()) != null) list.add(l);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return list.stream();
    }

    public void close() throws IOException { in.close(); }
}

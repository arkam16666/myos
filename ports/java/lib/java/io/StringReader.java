package java.io;

public class StringReader extends Reader {
    private String str;
    private int next;

    public StringReader(String s) { str = s; }

    public int read() {
        return next < str.length() ? str.charAt(next++) : -1;
    }

    public int read(char[] cbuf, int off, int len) {
        if (len == 0) return 0;
        if (next >= str.length()) return -1;
        int n = Math.min(str.length() - next, len);
        str.getChars(next, next + n, cbuf, off);
        next += n;
        return n;
    }

    public boolean ready() { return true; }

    public void close() {}
}

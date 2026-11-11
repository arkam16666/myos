package java.util;

import java.io.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Reads tokens and lines from System.in, a file or a string. */
public final class Scanner implements Iterator<String>, Closeable {
    private Reader source;
    private char[] buf = new char[256];
    private int pos, len;
    private boolean eof, closed;
    private Pattern delim;          /* null = whitespace */
    private int radix = 10;

    public Scanner(InputStream source) { this.source = new InputStreamReader(source); }

    public Scanner(InputStream source, String charsetName) { this(source); }

    public Scanner(InputStream source, java.nio.charset.Charset cs) { this(source); }

    public Scanner(Reader source) { this.source = source; }

    public Scanner(File source) throws FileNotFoundException { this.source = new InputStreamReader(new FileInputStream(source)); }

    public Scanner(java.nio.file.Path source) throws IOException { this(new File(source.toString())); }

    public Scanner(String source) {
        buf = source.toCharArray();
        len = buf.length;
        eof = true;
    }

    private void ensureOpen() {
        if (closed) throw new IllegalStateException("Scanner closed");
    }

    /* read more input; false at the end */
    private boolean more() {
        if (eof) return false;
        if (pos > 0 && pos == len) {
            pos = len = 0;
        } else if (pos > buf.length / 2) {
            System.arraycopy(buf, pos, buf, 0, len - pos);
            len -= pos;
            pos = 0;
        }
        if (len == buf.length) buf = Arrays.copyOf(buf, buf.length * 2);
        int n;
        try {
            n = source.read(buf, len, buf.length - len);
        } catch (IOException e) {
            n = -1;
        }
        if (n <= 0) {
            eof = true;
            return false;
        }
        len += n;
        return true;
    }

    private boolean isDelim(int i) {
        if (delim == null) return Character.isWhitespace(buf[i]);
        return false;
    }

    /* length of the delimiter run starting at i (custom delimiters) */
    private int delimAt(int i) {
        if (delim == null) {
            int k = i;
            while (k < len && Character.isWhitespace(buf[k])) k++;
            return k - i;
        }
        Matcher m = delim.matcher(new String(buf, 0, len));
        m.region(i, len);
        if (m.lookingAt() && m.end() > i) return m.end() - i;
        return 0;
    }

    /* the next token as [start, end) without consuming it; null at the end */
    private int[] peekToken() {
        ensureOpen();
        /* offsets from pos: more() may move the buffer contents */
        int s = 0;
        for (;;) {
            s += delimAt(pos + s);
            if (pos + s < len) break;
            if (!more()) return null;
        }
        for (;;) {
            int start = pos + s, end = start;
            if (delim == null) {
                while (end < len && !Character.isWhitespace(buf[end])) end++;
            } else {
                while (end < len && delimAt(end) == 0) end++;
            }
            if (end < len || eof) return new int[] { start, end };
            if (!more()) return new int[] { pos + s, len };
        }
    }

    public boolean hasNext() { return peekToken() != null; }

    public String next() {
        int[] t = peekToken();
        if (t == null) throw new NoSuchElementException();
        pos = t[1];
        return new String(buf, t[0], t[1] - t[0]);
    }

    public boolean hasNext(String pattern) {
        int[] t = peekToken();
        return t != null && new String(buf, t[0], t[1] - t[0]).matches(pattern);
    }

    public String next(String pattern) {
        if (!hasNext(pattern)) throw new InputMismatchException();
        return next();
    }

    public boolean hasNextLine() {
        ensureOpen();
        return pos < len || more();
    }

    public String nextLine() {
        ensureOpen();
        int k = 0;
        for (;;) {
            while (pos + k < len && buf[pos + k] != '\n') k++;
            if (pos + k < len) break;
            if (!more()) {
                if (pos >= len) throw new NoSuchElementException("No line found");
                String s = new String(buf, pos, len - pos);
                pos = len;
                return s;
            }
        }
        int i = pos + k, end = i;
        if (end > pos && buf[end - 1] == '\r') end--;
        String s = new String(buf, pos, end - pos);
        pos = i + 1;
        return s;
    }

    private String tokenOrNull() {
        int[] t = peekToken();
        return t == null ? null : new String(buf, t[0], t[1] - t[0]);
    }

    private String numberToken() {
        String s = tokenOrNull();
        if (s == null) throw new NoSuchElementException();
        return s;
    }

    private void consume() { pos = peekToken()[1]; }

    public boolean hasNextInt() {
        String s = tokenOrNull();
        if (s == null) return false;
        try {
            Integer.parseInt(s, radix);
            return true;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    public int nextInt() { return nextInt(radix); }

    public int nextInt(int radix) {
        String s = numberToken();
        try {
            int v = Integer.parseInt(s, radix);
            consume();
            return v;
        } catch (NumberFormatException e) {
            throw new InputMismatchException("For input string: \"" + s + "\"");
        }
    }

    public boolean hasNextLong() {
        String s = tokenOrNull();
        if (s == null) return false;
        try {
            Long.parseLong(s, radix);
            return true;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    public long nextLong() {
        String s = numberToken();
        try {
            long v = Long.parseLong(s, radix);
            consume();
            return v;
        } catch (NumberFormatException e) {
            throw new InputMismatchException("For input string: \"" + s + "\"");
        }
    }

    public short nextShort() { return (short) nextInt(); }

    public byte nextByte() { return (byte) nextInt(); }

    private static boolean numeric(String s) {
        if (s.isEmpty()) return false;
        char c = s.charAt(s.length() - 1);
        return Character.isDigit(c) || c == '.' || s.equals("NaN") || s.endsWith("Infinity");
    }

    public boolean hasNextDouble() {
        String s = tokenOrNull();
        if (s == null || !numeric(s)) return false;
        try {
            Double.parseDouble(s);
            return true;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    public double nextDouble() {
        String s = numberToken();
        if (!numeric(s)) throw new InputMismatchException("For input string: \"" + s + "\"");
        try {
            double v = Double.parseDouble(s);
            consume();
            return v;
        } catch (NumberFormatException e) {
            throw new InputMismatchException("For input string: \"" + s + "\"");
        }
    }

    public boolean hasNextFloat() { return hasNextDouble(); }

    public float nextFloat() { return (float) nextDouble(); }

    public boolean hasNextBoolean() {
        String s = tokenOrNull();
        return s != null && (s.equalsIgnoreCase("true") || s.equalsIgnoreCase("false"));
    }

    public boolean nextBoolean() {
        String s = numberToken();
        if (!s.equalsIgnoreCase("true") && !s.equalsIgnoreCase("false"))
            throw new InputMismatchException("For input string: \"" + s + "\"");
        consume();
        return s.equalsIgnoreCase("true");
    }

    public Scanner useDelimiter(String pattern) {
        delim = Pattern.compile(pattern);
        return this;
    }

    public Scanner useDelimiter(Pattern pattern) {
        delim = pattern;
        return this;
    }

    public Pattern delimiter() { return delim == null ? Pattern.compile("\\p{javaWhitespace}+") : delim; }

    public Scanner useLocale(Locale l) { return this; }

    public Scanner useRadix(int radix) {
        this.radix = radix;
        return this;
    }

    public Scanner skip(String pattern) {
        Matcher m = Pattern.compile(pattern).matcher(new String(buf, 0, len));
        m.region(pos, len);
        if (!m.lookingAt()) throw new NoSuchElementException();
        pos = m.end();
        return this;
    }

    public java.util.stream.Stream<String> tokens() {
        ArrayList<String> l = new ArrayList<String>();
        while (hasNext()) l.add(next());
        return l.stream();
    }

    public void remove() { throw new UnsupportedOperationException(); }

    public void close() {
        if (closed) return;
        closed = true;
        if (source != null) {
            try {
                source.close();
            } catch (IOException e) {
                /* ignore */
            }
        }
    }
}

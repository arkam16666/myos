package java.lang;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.Locale;

public final class String implements java.io.Serializable, Comparable<String>, CharSequence {
    final char[] value;
    private int hash;

    public static final Comparator<String> CASE_INSENSITIVE_ORDER = new Comparator<String>() {
        public int compare(String a, String b) { return a.compareToIgnoreCase(b); }
    };

    public String() { value = new char[0]; }

    public String(String original) { value = original.value; hash = original.hash; }

    public String(char[] v) { value = java.util.Arrays.copyOf(v, v.length); }

    public String(char[] v, int offset, int count) {
        checkRange(offset, count, v.length);
        value = java.util.Arrays.copyOfRange(v, offset, offset + count);
    }

    public String(int[] codePoints, int offset, int count) {
        StringBuilder sb = new StringBuilder();
        for (int i = offset; i < offset + count; i++) sb.appendCodePoint(codePoints[i]);
        value = sb.toString().value;
    }

    public String(byte[] bytes) { this(bytes, 0, bytes.length); }

    public String(byte[] bytes, int offset, int length) {
        checkRange(offset, length, bytes.length);
        value = decodeUtf8(bytes, offset, length);
    }

    public String(byte[] bytes, String charsetName) throws java.io.UnsupportedEncodingException {
        this(bytes, 0, bytes.length, charsetName);
    }

    public String(byte[] bytes, int offset, int length, String charsetName) throws java.io.UnsupportedEncodingException {
        checkRange(offset, length, bytes.length);
        value = decode(bytes, offset, length, java.nio.charset.Charset.lookup(charsetName));
    }

    public String(byte[] bytes, java.nio.charset.Charset cs) { this(bytes, 0, bytes.length, cs); }

    public String(byte[] bytes, int offset, int length, java.nio.charset.Charset cs) {
        checkRange(offset, length, bytes.length);
        value = decode(bytes, offset, length, cs);
    }

    public String(StringBuilder sb) { value = sb.toString().value; }

    public String(StringBuffer sb) { value = sb.toString().value; }

    String(char[] v, boolean share) { value = v; }

    private static void checkRange(int offset, int count, int length) {
        if (offset < 0 || count < 0 || offset > length - count)
            throw new StringIndexOutOfBoundsException("offset " + offset + ", count " + count + ", length " + length);
    }

    private static char[] decode(byte[] b, int off, int len, java.nio.charset.Charset cs) {
        if (cs == null || cs.isUtf8()) return decodeUtf8(b, off, len);
        char[] c = new char[len];
        boolean ascii = cs.name().equals("US-ASCII");
        for (int i = 0; i < len; i++) {
            int v = b[off + i] & 255;
            c[i] = ascii && v > 127 ? '\ufffd' : (char) v;
        }
        return c;
    }

    static char[] decodeUtf8(byte[] b, int off, int len) {
        char[] out = new char[len];
        int n = 0, end = off + len, i = off;
        while (i < end) {
            int c = b[i++] & 255;
            if (c < 0x80) {
                out[n++] = (char) c;
                continue;
            }
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (more == 0) {
                out[n++] = '\ufffd';
                continue;
            }
            int cp = c & (0x3F >> more);
            boolean bad = false;
            for (int k = 0; k < more; k++) {
                if (i >= end || (b[i] & 0xC0) != 0x80) {
                    bad = true;
                    break;
                }
                cp = cp << 6 | (b[i++] & 0x3F);
            }
            if (bad) out[n++] = '\ufffd';
            else if (cp >= 0x10000) {
                if (n + 2 > out.length) out = java.util.Arrays.copyOf(out, out.length + 2);
                cp -= 0x10000;
                out[n++] = (char) (0xD800 | (cp >> 10));
                out[n++] = (char) (0xDC00 | (cp & 0x3FF));
            } else out[n++] = (char) cp;
        }
        return n == out.length ? out : java.util.Arrays.copyOf(out, n);
    }

    static byte[] encodeUtf8(char[] v, int off, int len) {
        int n = 0;
        for (int i = off; i < off + len; i++) {
            char c = v[i];
            if (c < 0x80) n++;
            else if (c < 0x800) n += 2;
            else if (Character.isHighSurrogate(c) && i + 1 < off + len && Character.isLowSurrogate(v[i + 1])) {
                n += 4;
                i++;
            } else n += 3;
        }
        byte[] b = new byte[n];
        int k = 0;
        for (int i = off; i < off + len; i++) {
            int c = v[i];
            if (c < 0x80) b[k++] = (byte) c;
            else if (c < 0x800) {
                b[k++] = (byte) (0xC0 | c >> 6);
                b[k++] = (byte) (0x80 | (c & 63));
            } else if (Character.isHighSurrogate((char) c) && i + 1 < off + len && Character.isLowSurrogate(v[i + 1])) {
                int cp = Character.toCodePoint((char) c, v[++i]);
                b[k++] = (byte) (0xF0 | cp >> 18);
                b[k++] = (byte) (0x80 | ((cp >> 12) & 63));
                b[k++] = (byte) (0x80 | ((cp >> 6) & 63));
                b[k++] = (byte) (0x80 | (cp & 63));
            } else {
                b[k++] = (byte) (0xE0 | c >> 12);
                b[k++] = (byte) (0x80 | ((c >> 6) & 63));
                b[k++] = (byte) (0x80 | (c & 63));
            }
        }
        return b;
    }

    public int length() { return value.length; }

    public boolean isEmpty() { return value.length == 0; }

    public char charAt(int index) {
        if (index < 0 || index >= value.length)
            throw new StringIndexOutOfBoundsException("Index " + index + " out of bounds for length " + value.length);
        return value[index];
    }

    public int codePointAt(int index) { return Character.codePointAt(value, index); }

    public int codePointCount(int begin, int end) {
        int n = 0;
        for (int i = begin; i < end; i++, n++)
            if (Character.isHighSurrogate(value[i]) && i + 1 < end && Character.isLowSurrogate(value[i + 1])) i++;
        return n;
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > value.length || srcBegin > srcEnd)
            throw new StringIndexOutOfBoundsException("begin " + srcBegin + ", end " + srcEnd + ", length " + value.length);
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public byte[] getBytes() { return encodeUtf8(value, 0, value.length); }

    public byte[] getBytes(String charsetName) throws java.io.UnsupportedEncodingException {
        return getBytes(java.nio.charset.Charset.lookup(charsetName));
    }

    public byte[] getBytes(java.nio.charset.Charset cs) {
        if (cs == null || cs.isUtf8()) return getBytes();
        byte[] b = new byte[value.length];
        int max = cs.name().equals("US-ASCII") ? 127 : 255;
        for (int i = 0; i < b.length; i++) b[i] = (byte) (value[i] <= max ? value[i] : '?');
        return b;
    }

    public boolean equals(Object anObject) {
        if (this == anObject) return true;
        if (!(anObject instanceof String)) return false;
        char[] a = value, b = ((String) anObject).value;
        int n = a.length;
        if (n != b.length) return false;
        for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public boolean contentEquals(CharSequence cs) {
        if (cs.length() != value.length) return false;
        for (int i = 0; i < value.length; i++) if (value[i] != cs.charAt(i)) return false;
        return true;
    }

    public boolean contentEquals(StringBuffer sb) { return contentEquals((CharSequence) sb); }

    public boolean equalsIgnoreCase(String other) {
        if (other == null || other.value.length != value.length) return false;
        return regionMatches(true, 0, other, 0, value.length);
    }

    public int compareTo(String other) {
        char[] a = value, b = other.value;
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) if (a[i] != b[i]) return a[i] - b[i];
        return a.length - b.length;
    }

    public int compareToIgnoreCase(String other) {
        char[] a = value, b = other.value;
        int n = Math.min(a.length, b.length);
        for (int i = 0; i < n; i++) {
            char x = a[i], y = b[i];
            if (x != y) {
                x = Character.toLowerCase(Character.toUpperCase(x));
                y = Character.toLowerCase(Character.toUpperCase(y));
                if (x != y) return x - y;
            }
        }
        return a.length - b.length;
    }

    public boolean regionMatches(int toffset, String other, int ooffset, int len) {
        return regionMatches(false, toffset, other, ooffset, len);
    }

    public boolean regionMatches(boolean ignoreCase, int toffset, String other, int ooffset, int len) {
        if (ooffset < 0 || toffset < 0 || toffset > (long) value.length - len || ooffset > (long) other.value.length - len)
            return false;
        for (int i = 0; i < len; i++) {
            char a = value[toffset + i], b = other.value[ooffset + i];
            if (a == b) continue;
            if (ignoreCase) {
                char ua = Character.toUpperCase(a), ub = Character.toUpperCase(b);
                if (ua == ub || Character.toLowerCase(ua) == Character.toLowerCase(ub)) continue;
            }
            return false;
        }
        return true;
    }

    public boolean startsWith(String prefix, int toffset) {
        char[] p = prefix.value;
        if (toffset < 0 || toffset > value.length - p.length) return false;
        for (int i = 0; i < p.length; i++) if (value[toffset + i] != p[i]) return false;
        return true;
    }

    public boolean startsWith(String prefix) { return startsWith(prefix, 0); }

    public boolean endsWith(String suffix) { return startsWith(suffix, value.length - suffix.value.length); }

    public int hashCode() {
        int h = hash;
        if (h == 0 && value.length > 0) {
            for (char c : value) h = 31 * h + c;
            hash = h;
        }
        return h;
    }

    public int indexOf(int ch) { return indexOf(ch, 0); }

    public int indexOf(int ch, int fromIndex) {
        if (fromIndex < 0) fromIndex = 0;
        if (ch >= 0x10000) return indexOf(new String(Character.toChars(ch), true), fromIndex);
        char[] v = value;
        for (int i = fromIndex; i < v.length; i++) if (v[i] == ch) return i;
        return -1;
    }

    public int lastIndexOf(int ch) { return lastIndexOf(ch, value.length - 1); }

    public int lastIndexOf(int ch, int fromIndex) {
        if (ch >= 0x10000) return lastIndexOf(new String(Character.toChars(ch), true), fromIndex);
        for (int i = Math.min(fromIndex, value.length - 1); i >= 0; i--) if (value[i] == ch) return i;
        return -1;
    }

    public int indexOf(String str) { return indexOf(str, 0); }

    public int indexOf(String str, int fromIndex) {
        char[] v = value, s = str.value;
        if (fromIndex < 0) fromIndex = 0;
        int n = s.length, last = v.length - n;
        if (n == 0) return fromIndex <= v.length ? fromIndex : v.length;
        char first = s[0];
        outer:
        for (int i = fromIndex; i <= last; i++) {
            if (v[i] != first) continue;
            for (int k = 1; k < n; k++) if (v[i + k] != s[k]) continue outer;
            return i;
        }
        return -1;
    }

    public int lastIndexOf(String str) { return lastIndexOf(str, value.length); }

    public int lastIndexOf(String str, int fromIndex) {
        char[] v = value, s = str.value;
        int n = s.length;
        int start = Math.min(fromIndex, v.length - n);
        outer:
        for (int i = start; i >= 0; i--) {
            for (int k = 0; k < n; k++) if (v[i + k] != s[k]) continue outer;
            return i;
        }
        return -1;
    }

    public String substring(int beginIndex) { return substring(beginIndex, value.length); }

    public String substring(int beginIndex, int endIndex) {
        int len = value.length;
        if (beginIndex < 0 || endIndex > len || beginIndex > endIndex)
            throw new StringIndexOutOfBoundsException("begin " + beginIndex + ", end " + endIndex + ", length " + len);
        if (beginIndex == 0 && endIndex == len) return this;
        return new String(java.util.Arrays.copyOfRange(value, beginIndex, endIndex), true);
    }

    public CharSequence subSequence(int beginIndex, int endIndex) { return substring(beginIndex, endIndex); }

    public String concat(String str) {
        if (str.value.length == 0) return this;
        char[] c = java.util.Arrays.copyOf(value, value.length + str.value.length);
        System.arraycopy(str.value, 0, c, value.length, str.value.length);
        return new String(c, true);
    }

    public String replace(char oldChar, char newChar) {
        if (oldChar == newChar || indexOf(oldChar) < 0) return this;
        char[] c = value.clone();
        for (int i = 0; i < c.length; i++) if (c[i] == oldChar) c[i] = newChar;
        return new String(c, true);
    }

    public String replace(CharSequence target, CharSequence replacement) {
        String t = target.toString(), r = replacement.toString();
        if (t.isEmpty()) {
            StringBuilder sb = new StringBuilder(r);
            for (char c : value) sb.append(c).append(r);
            return sb.toString();
        }
        int i = indexOf(t);
        if (i < 0) return this;
        StringBuilder sb = new StringBuilder();
        int from = 0;
        while (i >= 0) {
            sb.append(value, from, i - from).append(r);
            from = i + t.length();
            i = indexOf(t, from);
        }
        sb.append(value, from, value.length - from);
        return sb.toString();
    }

    public boolean matches(String regex) { return java.util.regex.Pattern.matches(regex, this); }

    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }

    public String replaceFirst(String regex, String replacement) {
        return java.util.regex.Pattern.compile(regex).matcher(this).replaceFirst(replacement);
    }

    public String replaceAll(String regex, String replacement) {
        return java.util.regex.Pattern.compile(regex).matcher(this).replaceAll(replacement);
    }

    public String[] split(String regex) { return split(regex, 0); }

    public String[] split(String regex, int limit) {
        /* fast path: one ordinary character, or a backslash and a non-alphanumeric one */
        char ch = 0;
        int rl = regex.length();
        if ((rl == 1 && ".$|()[{^?*+\\".indexOf(ch = regex.charAt(0)) < 0)
                || (rl == 2 && regex.charAt(0) == '\\' && !Character.isLetterOrDigit(ch = regex.charAt(1)))) {
            ArrayList<String> list = new ArrayList<String>();
            int off = 0, next;
            boolean limited = limit > 0;
            while ((next = indexOf(ch, off)) != -1) {
                if (!limited || list.size() < limit - 1) {
                    list.add(substring(off, next));
                    off = next + 1;
                } else break;
            }
            if (off == 0) return new String[] { this };
            list.add(substring(off, value.length));
            int size = list.size();
            if (limit == 0) while (size > 0 && list.get(size - 1).isEmpty()) size--;
            String[] r = new String[size];
            for (int i = 0; i < size; i++) r[i] = list.get(i);
            return r;
        }
        return java.util.regex.Pattern.compile(regex).split(this, limit);
    }

    public static String join(CharSequence delimiter, CharSequence... elements) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < elements.length; i++) {
            if (i > 0) sb.append(delimiter);
            sb.append(elements[i]);
        }
        return sb.toString();
    }

    public static String join(CharSequence delimiter, Iterable<? extends CharSequence> elements) {
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (CharSequence cs : elements) {
            if (!first) sb.append(delimiter);
            sb.append(cs);
            first = false;
        }
        return sb.toString();
    }

    public String toLowerCase(Locale locale) { return toLowerCase(); }

    public String toLowerCase() {
        char[] c = null;
        for (int i = 0; i < value.length; i++) {
            char l = Character.toLowerCase(value[i]);
            if (l != value[i]) {
                if (c == null) c = value.clone();
                c[i] = l;
            }
        }
        return c == null ? this : new String(c, true);
    }

    public String toUpperCase(Locale locale) { return toUpperCase(); }

    public String toUpperCase() {
        char[] c = null;
        for (int i = 0; i < value.length; i++) {
            char u = Character.toUpperCase(value[i]);
            if (u != value[i]) {
                if (c == null) c = value.clone();
                c[i] = u;
            }
        }
        return c == null ? this : new String(c, true);
    }

    public String trim() {
        int a = 0, b = value.length;
        while (a < b && value[a] <= ' ') a++;
        while (b > a && value[b - 1] <= ' ') b--;
        return substring(a, b);
    }

    public String strip() {
        int a = 0, b = value.length;
        while (a < b && Character.isWhitespace(value[a])) a++;
        while (b > a && Character.isWhitespace(value[b - 1])) b--;
        return substring(a, b);
    }

    public String stripLeading() {
        int a = 0;
        while (a < value.length && Character.isWhitespace(value[a])) a++;
        return substring(a);
    }

    public String stripTrailing() {
        int b = value.length;
        while (b > 0 && Character.isWhitespace(value[b - 1])) b--;
        return substring(0, b);
    }

    public boolean isBlank() {
        for (char c : value) if (!Character.isWhitespace(c)) return false;
        return true;
    }

    public java.util.stream.Stream<String> lines() {
        ArrayList<String> list = new ArrayList<String>();
        int start = 0, n = value.length;
        for (int i = 0; i < n; i++) {
            char c = value[i];
            if (c == '\n' || c == '\r') {
                list.add(substring(start, i));
                if (c == '\r' && i + 1 < n && value[i + 1] == '\n') i++;
                start = i + 1;
            }
        }
        if (start < n) list.add(substring(start));
        return list.stream();
    }

    public String indent(int n) {
        StringBuilder sb = new StringBuilder();
        for (Iterator<String> it = lines().iterator(); it.hasNext();) {
            String l = it.next();
            if (n > 0) sb.append(" ".repeat(n)).append(l);
            else {
                int k = 0;
                while (k < -n && k < l.length() && Character.isWhitespace(l.charAt(k))) k++;
                sb.append(l, k, l.length());
            }
            sb.append('\n');
        }
        return sb.toString();
    }

    public String repeat(int count) {
        if (count < 0) throw new IllegalArgumentException("count is negative: " + count);
        if (count == 1) return this;
        int n = value.length;
        char[] c = new char[n * count];
        for (int i = 0; i < count; i++) System.arraycopy(value, 0, c, i * n, n);
        return new String(c, true);
    }

    public String toString() { return this; }

    public char[] toCharArray() { return value.clone(); }

    public java.util.stream.IntStream chars() {
        int[] a = new int[value.length];
        for (int i = 0; i < a.length; i++) a[i] = value[i];
        return java.util.stream.IntStream.of(a);
    }

    public java.util.stream.IntStream codePoints() {
        int[] a = new int[codePointCount(0, value.length)];
        for (int i = 0, k = 0; i < value.length; k++) {
            int cp = Character.codePointAt(value, i);
            a[k] = cp;
            i += Character.charCount(cp);
        }
        return java.util.stream.IntStream.of(a);
    }

    public static String format(String format, Object... args) { return new java.util.Formatter().format(format, args).toString(); }

    public static String format(Locale l, String format, Object... args) { return format(format, args); }

    public String formatted(Object... args) { return format(this, args); }

    public static String valueOf(Object obj) { return obj == null ? "null" : obj.toString(); }

    public static String valueOf(char[] data) { return new String(data); }

    public static String valueOf(char[] data, int offset, int count) { return new String(data, offset, count); }

    public static String copyValueOf(char[] data) { return new String(data); }

    public static String copyValueOf(char[] data, int offset, int count) { return new String(data, offset, count); }

    public static String valueOf(boolean b) { return b ? "true" : "false"; }

    public static String valueOf(char c) { return new String(new char[] { c }, true); }

    public static String valueOf(int i) { return Integer.toString(i); }

    public static String valueOf(long l) { return Long.toString(l); }

    public static String valueOf(float f) { return Float.toString(f); }

    public static String valueOf(double d) { return Double.toString(d); }

    public native String intern();
}

package java.lang;

public final class StringBuffer implements java.io.Serializable, Comparable<StringBuffer>, CharSequence, Appendable {
    char[] value;
    int count;

    public StringBuffer() { value = new char[16]; }

    public StringBuffer(int capacity) { value = new char[capacity]; }

    public StringBuffer(String str) {
        value = new char[str.length() + 16];
        append(str);
    }

    public StringBuffer(CharSequence seq) {
        value = new char[seq.length() + 16];
        append(seq);
    }

    private void grow(int need) {
        if (need > value.length) {
            int n = value.length * 2 + 2;
            if (n < need) n = need;
            value = java.util.Arrays.copyOf(value, n);
        }
    }

    public int length() { return count; }

    public int capacity() { return value.length; }

    public void ensureCapacity(int minimumCapacity) { grow(minimumCapacity); }

    public void trimToSize() { if (count < value.length) value = java.util.Arrays.copyOf(value, count); }

    public void setLength(int newLength) {
        if (newLength < 0) throw new StringIndexOutOfBoundsException(newLength);
        grow(newLength);
        for (int i = count; i < newLength; i++) value[i] = 0;
        count = newLength;
    }

    public char charAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ",length " + count);
        return value[index];
    }

    public void setCharAt(int index, char ch) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ",length " + count);
        value[index] = ch;
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd) throw new StringIndexOutOfBoundsException(srcBegin);
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public StringBuffer append(Object obj) { return append(String.valueOf(obj)); }

    public StringBuffer append(String str) {
        if (str == null) str = "null";
        char[] s = str.value;
        int n = s.length;
        grow(count + n);
        System.arraycopy(s, 0, value, count, n);
        count += n;
        return this;
    }

    public StringBuffer append(StringBuffer sb) { return append((CharSequence) sb); }

    public StringBuffer append(StringBuilder sb) { return append((CharSequence) sb); }

    public StringBuffer append(CharSequence s) {
        if (s == null) return append("null");
        if (s instanceof String) return append((String) s);
        return append(s, 0, s.length());
    }

    public StringBuffer append(CharSequence s, int start, int end) {
        if (s == null) s = "null";
        if (start < 0 || start > end || end > s.length())
            throw new IndexOutOfBoundsException("start " + start + ", end " + end + ", length " + s.length());
        grow(count + end - start);
        for (int i = start; i < end; i++) value[count++] = s.charAt(i);
        return this;
    }

    public StringBuffer append(char[] str) { return append(str, 0, str.length); }

    public StringBuffer append(char[] str, int offset, int len) {
        grow(count + len);
        System.arraycopy(str, offset, value, count, len);
        count += len;
        return this;
    }

    public StringBuffer append(boolean b) { return append(b ? "true" : "false"); }

    public StringBuffer append(char c) {
        if (count == value.length) grow(count + 1);
        value[count++] = c;
        return this;
    }

    public StringBuffer append(int i) {
        if (i >= 0 && i < 10) return append((char) ('0' + i));
        return append(Integer.toString(i));
    }

    public StringBuffer append(long lng) { return append(Long.toString(lng)); }

    public StringBuffer append(float f) { return append(Float.toString(f)); }

    public StringBuffer append(double d) { return append(Double.toString(d)); }

    public StringBuffer appendCodePoint(int codePoint) { return append(Character.toChars(codePoint)); }

    public StringBuffer delete(int start, int end) {
        if (end > count) end = count;
        if (start < 0 || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end + ", length " + count);
        System.arraycopy(value, end, value, start, count - end);
        count -= end - start;
        return this;
    }

    public StringBuffer deleteCharAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException("index " + index + ",length " + count);
        return delete(index, index + 1);
    }

    public StringBuffer replace(int start, int end, String str) {
        if (end > count) end = count;
        if (start < 0 || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end + ", length " + count);
        int n = str.length(), newCount = count + n - (end - start);
        grow(newCount);
        System.arraycopy(value, end, value, start + n, count - end);
        str.getChars(0, n, value, start);
        count = newCount;
        return this;
    }

    public String substring(int start) { return substring(start, count); }

    public CharSequence subSequence(int start, int end) { return substring(start, end); }

    public String substring(int start, int end) {
        if (start < 0 || end > count || start > end) throw new StringIndexOutOfBoundsException("start " + start + ", end " + end + ", length " + count);
        return new String(value, start, end - start);
    }

    public StringBuffer insert(int index, char[] str, int offset, int len) {
        return insert(index, new String(str, offset, len));
    }

    public StringBuffer insert(int offset, Object obj) { return insert(offset, String.valueOf(obj)); }

    public StringBuffer insert(int offset, String str) {
        if (offset < 0 || offset > count) throw new StringIndexOutOfBoundsException("offset " + offset + ", length " + count);
        if (str == null) str = "null";
        int n = str.length();
        grow(count + n);
        System.arraycopy(value, offset, value, offset + n, count - offset);
        str.getChars(0, n, value, offset);
        count += n;
        return this;
    }

    public StringBuffer insert(int offset, char[] str) { return insert(offset, new String(str)); }

    public StringBuffer insert(int dstOffset, CharSequence s) { return insert(dstOffset, String.valueOf(s)); }

    public StringBuffer insert(int offset, boolean b) { return insert(offset, String.valueOf(b)); }

    public StringBuffer insert(int offset, char c) { return insert(offset, String.valueOf(c)); }

    public StringBuffer insert(int offset, int i) { return insert(offset, String.valueOf(i)); }

    public StringBuffer insert(int offset, long l) { return insert(offset, String.valueOf(l)); }

    public StringBuffer insert(int offset, float f) { return insert(offset, String.valueOf(f)); }

    public StringBuffer insert(int offset, double d) { return insert(offset, String.valueOf(d)); }

    public int indexOf(String str) { return toString().indexOf(str); }

    public int indexOf(String str, int fromIndex) { return toString().indexOf(str, fromIndex); }

    public int lastIndexOf(String str) { return toString().lastIndexOf(str); }

    public int lastIndexOf(String str, int fromIndex) { return toString().lastIndexOf(str, fromIndex); }

    public StringBuffer reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            char t = value[i];
            value[i] = value[j];
            value[j] = t;
        }
        for (int i = 0; i < count - 1; i++) {      /* keep surrogate pairs in order */
            if (Character.isLowSurrogate(value[i]) && Character.isHighSurrogate(value[i + 1])) {
                char t = value[i];
                value[i] = value[i + 1];
                value[i + 1] = t;
                i++;
            }
        }
        return this;
    }

    public int compareTo(StringBuffer another) { return toString().compareTo(another.toString()); }

    public java.util.stream.IntStream chars() { return toString().chars(); }

    public String toString() { return new String(java.util.Arrays.copyOf(value, count), true); }
}

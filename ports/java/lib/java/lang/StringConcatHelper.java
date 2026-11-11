package java.lang;

/** Used by the VM for invokedynamic string concatenation (javac 9+). */
final class StringConcatHelper {
    private StringConcatHelper() {}

    static String concat(Object[] parts) {
        int n = parts.length, len = 0;
        String[] s = new String[n];
        for (int i = 0; i < n; i++) {
            Object o = parts[i];
            String t = o == null ? "null" : o.toString();
            if (t == null) t = "null";
            s[i] = t;
            len += t.value.length;
        }
        char[] buf = new char[len];
        int at = 0;
        for (int i = 0; i < n; i++) {
            char[] v = s[i].value;
            System.arraycopy(v, 0, buf, at, v.length);
            at += v.length;
        }
        return new String(buf, true);
    }
}

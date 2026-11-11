package java.util;

/** printf-style formatting for String.format, printf and friends. */
public final class Formatter implements java.io.Closeable, java.io.Flushable {
    private final StringBuilder out;

    public Formatter() { out = new StringBuilder(); }

    public Formatter(StringBuilder sb) { out = sb == null ? new StringBuilder() : sb; }

    public Formatter(Locale l) { this(); }

    public Formatter(Appendable a) { out = a instanceof StringBuilder ? (StringBuilder) a : new StringBuilder(); }

    public Locale locale() { return Locale.US; }

    public Appendable out() { return out; }

    public String toString() { return out.toString(); }

    public void flush() {}

    public void close() {}

    private static native String fmtDouble(double v, char conv, int prec);

    public Formatter format(Locale l, String format, Object... args) { return format(format, args); }

    public Formatter format(String format, Object... args) {
        int n = format.length(), argi = 0, last = -1;
        int i = 0;
        while (i < n) {
            char c = format.charAt(i);
            if (c != '%') {
                out.append(c);
                i++;
                continue;
            }
            int start = i++;
            if (i >= n) throw new UnknownFormatConversionException("%");
            /* %[index$][flags][width][.precision]conversion */
            int index = -1;
            int j = i;
            while (j < n && Character.isDigit(format.charAt(j))) j++;
            if (j < n && format.charAt(j) == '$' && j > i) {
                index = Integer.parseInt(format.substring(i, j));
                i = j + 1;
            }
            String flags = "";
            while (i < n && "-#+ 0,(<".indexOf(format.charAt(i)) >= 0) flags += format.charAt(i++);
            int width = -1;
            j = i;
            while (j < n && Character.isDigit(format.charAt(j))) j++;
            if (j > i) {
                width = Integer.parseInt(format.substring(i, j));
                i = j;
            }
            int prec = -1;
            if (i < n && format.charAt(i) == '.') {
                j = ++i;
                while (j < n && Character.isDigit(format.charAt(j))) j++;
                if (j == i) throw new UnknownFormatConversionException(".");
                prec = Integer.parseInt(format.substring(i, j));
                i = j;
            }
            if (i >= n) throw new UnknownFormatConversionException(format.substring(start));
            char conv = format.charAt(i++);
            boolean dateTime = false;
            if (conv == 't' || conv == 'T') {
                if (i >= n) throw new UnknownFormatConversionException(String.valueOf(conv));
                dateTime = true;
                conv = format.charAt(i++);
            }
            if (conv == 'n') {
                out.append('\n');
                continue;
            }
            if (conv == '%') {
                pad("%", flags, width);
                continue;
            }
            Object arg;
            if (flags.indexOf('<') >= 0) {
                if (last < 0) throw new MissingFormatArgumentException(format.substring(start, i));
                arg = args == null ? null : args[last];
            } else {
                int k = index > 0 ? index - 1 : argi++;
                if (args == null) arg = null;
                else {
                    if (k >= args.length) throw new MissingFormatArgumentException(format.substring(start, i));
                    arg = args[k];
                }
                last = k;
            }
            if (dateTime) {
                pad(String.valueOf(arg), flags, width);
                continue;
            }
            formatOne(conv, flags, width, prec, arg);
        }
        return this;
    }

    private void pad(String s, String flags, int width) {
        if (width > s.length()) {
            if (flags.indexOf('-') >= 0) {
                out.append(s);
                for (int k = s.length(); k < width; k++) out.append(' ');
            } else {
                for (int k = s.length(); k < width; k++) out.append(' ');
                out.append(s);
            }
        } else out.append(s);
    }

    private static String group(String digits) {
        StringBuilder sb = new StringBuilder();
        int n = digits.length();
        for (int k = 0; k < n; k++) {
            if (k > 0 && (n - k) % 3 == 0) sb.append(',');
            sb.append(digits.charAt(k));
        }
        return sb.toString();
    }

    /* sign, digits (magnitude), flags -> padded number */
    private void number(boolean neg, String mag, String flags, int width, String prefix) {
        if (flags.indexOf(',') >= 0) {
            int dot = mag.indexOf('.');
            if (dot < 0) dot = mag.indexOf('e');
            if (dot < 0) mag = group(mag);
            else mag = group(mag.substring(0, dot)) + mag.substring(dot);
        }
        String sign = "";
        String suffix = "";
        if (neg) {
            if (flags.indexOf('(') >= 0) {
                sign = "(";
                suffix = ")";
            } else sign = "-";
        } else if (flags.indexOf('+') >= 0) sign = "+";
        else if (flags.indexOf(' ') >= 0) sign = " ";
        String body = prefix + mag;
        if (flags.indexOf('0') >= 0 && width > 0) {
            int len = sign.length() + body.length() + suffix.length();
            StringBuilder z = new StringBuilder();
            for (int k = len; k < width; k++) z.append('0');
            out.append(sign).append(prefix).append(z).append(mag).append(suffix);
            return;
        }
        pad(sign + body + suffix, flags, width);
    }

    private void formatOne(char conv, String flags, int width, int prec, Object arg) {
        boolean upper = Character.isUpperCase(conv);
        char lc = Character.toLowerCase(conv);
        String s;
        switch (lc) {
            case 'b':
                s = arg == null ? "false" : (arg instanceof Boolean ? arg.toString() : "true");
                if (prec >= 0 && prec < s.length()) s = s.substring(0, prec);
                break;
            case 'h':
                s = arg == null ? "null" : Integer.toHexString(arg.hashCode());
                break;
            case 's':
                s = String.valueOf(arg);
                if (prec >= 0 && prec < s.length()) s = s.substring(0, prec);
                break;
            case 'c':
                if (arg == null) s = "null";
                else if (arg instanceof Character) s = arg.toString();
                else if (arg instanceof Number) s = new String(Character.toChars(((Number) arg).intValue()));
                else throw new IllegalFormatException("%c can't format " + arg.getClass().getName());
                break;
            case 'd': {
                if (arg == null) {
                    s = "null";
                    break;
                }
                if (!(arg instanceof Integer || arg instanceof Long || arg instanceof Short || arg instanceof Byte))
                    throw new IllegalFormatException("d != " + arg.getClass().getName());
                long v = ((Number) arg).longValue();
                String mag = v == Long.MIN_VALUE ? "9223372036854775808" : Long.toString(Math.abs(v));
                number(v < 0, mag, flags, width, "");
                return;
            }
            case 'x':
            case 'o': {
                if (arg == null) {
                    s = "null";
                    break;
                }
                String digits;
                if (arg instanceof Long) digits = lc == 'x' ? Long.toHexString((Long) arg) : Long.toOctalString((Long) arg);
                else if (arg instanceof Integer) digits = lc == 'x' ? Integer.toHexString((Integer) arg) : Integer.toOctalString((Integer) arg);
                else if (arg instanceof Short) digits = Integer.toString(((Short) arg) & 0xffff, lc == 'x' ? 16 : 8);
                else if (arg instanceof Byte) digits = Integer.toString(((Byte) arg) & 0xff, lc == 'x' ? 16 : 8);
                else throw new IllegalFormatException(lc + " != " + arg.getClass().getName());
                String prefix = flags.indexOf('#') >= 0 ? (lc == 'x' ? "0x" : "0") : "";
                if (upper) {
                    digits = digits.toUpperCase();
                    prefix = prefix.toUpperCase();
                }
                number(false, digits, flags.replace("+", "").replace(" ", ""), width, prefix);
                return;
            }
            case 'e':
            case 'f':
            case 'g':
            case 'a': {
                if (arg == null) {
                    s = "null";
                    break;
                }
                if (!(arg instanceof Double || arg instanceof Float))
                    throw new IllegalFormatException(lc + " != " + arg.getClass().getName());
                double v = ((Number) arg).doubleValue();
                if (Double.isNaN(v) || Double.isInfinite(v)) {
                    String t = Double.isNaN(v) ? "NaN" : "Infinity";
                    boolean neg = v < 0;
                    if (Double.isNaN(v)) pad(t, flags, width);
                    else number(neg, t, flags.replace("0", "").replace(",", ""), width, "");
                    return;
                }
                boolean neg = v < 0 || (v == 0 && 1 / v < 0);
                String mag;
                if (prec < 0) prec = 6;
                if (lc == 'e') mag = fmtDouble(v, 'e', prec);
                else if (lc == 'f') mag = fmtDouble(v, 'f', prec);
                else if (lc == 'a') mag = Double.toHexString(Math.abs(v));
                else {
                    if (prec == 0) prec = 1;
                    double a = Math.abs(v);
                    if (a == 0) mag = fmtDouble(0, 'f', prec - 1);
                    else {
                        String e = fmtDouble(a, 'e', prec - 1);
                        int exp = Integer.parseInt(e.substring(e.indexOf('e') + 1));
                        if (exp >= -4 && exp < prec) mag = fmtDouble(a, 'f', prec - 1 - exp);
                        else mag = e;
                    }
                }
                if (upper) mag = mag.toUpperCase();
                number(neg, mag, flags, width, "");
                return;
            }
            default:
                throw new UnknownFormatConversionException(String.valueOf(conv));
        }
        if (upper) s = s.toUpperCase();
        pad(s, flags, width);
    }
}

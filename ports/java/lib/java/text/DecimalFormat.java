package java.text;

import java.math.BigDecimal;
import java.math.RoundingMode;

/** The usual DecimalFormat patterns: 0 # , . % and literal prefix/suffix text. */
public class DecimalFormat extends NumberFormat {
    private String prefix = "", suffix = "", negPrefix = null, negSuffix = null;
    private int minInt = 1, maxInt = Integer.MAX_VALUE, minFrac = 0, maxFrac = 3, grouping = 3;
    private boolean useGrouping = true, percent, permille, parseIntegerOnly, alwaysShowDecimal;
    private RoundingMode rounding = RoundingMode.HALF_EVEN;
    private String pattern;

    public DecimalFormat() { this("#,##0.###"); }

    public DecimalFormat(String pattern) { applyPattern(pattern); }

    public DecimalFormat(String pattern, DecimalFormatSymbols symbols) { this(pattern); }

    public void applyPattern(String p) {
        pattern = p;
        String pos = p, neg = null;
        int semi = p.indexOf(';');
        if (semi >= 0) {
            pos = p.substring(0, semi);
            neg = p.substring(semi + 1);
        }
        int i = 0, n = pos.length();
        StringBuilder pre = new StringBuilder();
        while (i < n && "0#,.".indexOf(pos.charAt(i)) < 0) {
            char c = pos.charAt(i++);
            if (c == '\'') {
                while (i < n && pos.charAt(i) != '\'') pre.append(pos.charAt(i++));
                i++;
            } else pre.append(c);
        }
        int numStart = i;
        while (i < n && "0#,.".indexOf(pos.charAt(i)) >= 0) i++;
        String num = pos.substring(numStart, i);
        StringBuilder suf = new StringBuilder();
        while (i < n) {
            char c = pos.charAt(i++);
            if (c == '\'') {
                while (i < n && pos.charAt(i) != '\'') suf.append(pos.charAt(i++));
                i++;
            } else suf.append(c);
        }
        prefix = pre.toString();
        suffix = suf.toString();
        percent = prefix.indexOf('%') >= 0 || suffix.indexOf('%') >= 0;
        permille = prefix.indexOf('\u2030') >= 0 || suffix.indexOf('\u2030') >= 0;
        int dot = num.indexOf('.');
        String ip = dot >= 0 ? num.substring(0, dot) : num, fp = dot >= 0 ? num.substring(dot + 1) : "";
        alwaysShowDecimal = dot >= 0 && fp.isEmpty();
        int lastComma = ip.lastIndexOf(',');
        useGrouping = lastComma >= 0;
        grouping = useGrouping ? ip.length() - lastComma - 1 : 3;
        String digits = ip.replace(",", "");
        minInt = 0;
        for (int k = 0; k < digits.length(); k++) if (digits.charAt(k) == '0') minInt++;
        minFrac = 0;
        maxFrac = fp.length();
        for (int k = 0; k < fp.length(); k++) if (fp.charAt(k) == '0') minFrac++;
        if (neg != null) {
            int a = 0;
            while (a < neg.length() && "0#,.".indexOf(neg.charAt(a)) < 0) a++;
            int b = a;
            while (b < neg.length() && "0#,.".indexOf(neg.charAt(b)) >= 0) b++;
            negPrefix = neg.substring(0, a).replace("'", "");
            negSuffix = neg.substring(b).replace("'", "");
        }
    }

    public String toPattern() { return pattern; }

    public StringBuffer format(double number, StringBuffer out, FieldPosition pos) {
        if (Double.isNaN(number)) return out.append("NaN");
        boolean neg = number < 0 || (number == 0 && 1 / number < 0);
        if (Double.isInfinite(number)) return out.append(neg ? "-" : "").append(prefix).append('\u221e').append(suffix);
        return formatBig(new BigDecimal(number), out, neg);
    }

    public StringBuffer format(long number, StringBuffer out, FieldPosition pos) {
        return formatBig(BigDecimal.valueOf(number), out, number < 0);
    }

    StringBuffer formatBig(BigDecimal v, StringBuffer out) { return formatBig(v, out, v.signum() < 0); }

    private StringBuffer formatBig(BigDecimal v, StringBuffer out, boolean neg) {
        if (percent) v = v.movePointRight(2);
        else if (permille) v = v.movePointRight(3);
        v = v.abs().setScale(maxFrac, rounding);
        String plain = v.toPlainString();
        int dot = plain.indexOf('.');
        String ip = dot >= 0 ? plain.substring(0, dot) : plain, fp = dot >= 0 ? plain.substring(dot + 1) : "";
        while (fp.length() > minFrac && fp.endsWith("0")) fp = fp.substring(0, fp.length() - 1);
        if (ip.equals("0")) ip = "";
        while (ip.length() < minInt) ip = "0" + ip;
        if (ip.length() > maxInt) ip = ip.substring(ip.length() - maxInt);
        if (useGrouping && grouping > 0) {
            StringBuilder g = new StringBuilder();
            for (int k = 0; k < ip.length(); k++) {
                if (k > 0 && (ip.length() - k) % grouping == 0) g.append(',');
                g.append(ip.charAt(k));
            }
            ip = g.toString();
        }
        String body = ip + (fp.isEmpty() && !alwaysShowDecimal ? "" : "." + fp);
        if (body.isEmpty()) body = "0";
        boolean zero = v.signum() == 0;
        if (neg) {
            if (negPrefix != null) return out.append(negPrefix).append(body).append(negSuffix);
            return out.append('-').append(prefix).append(body).append(suffix);
        }
        return out.append(prefix).append(body).append(suffix);
    }

    public Number parse(String source) throws ParseException {
        String s = source.trim();
        if (s.startsWith(prefix)) s = s.substring(prefix.length());
        if (!suffix.isEmpty() && s.endsWith(suffix)) s = s.substring(0, s.length() - suffix.length());
        s = s.replace(",", "");
        try {
            if (parseIntegerOnly || s.indexOf('.') < 0) {
                int dot = s.indexOf('.');
                return Long.parseLong(dot >= 0 ? s.substring(0, dot) : s);
            }
            double d = Double.parseDouble(s);
            if (percent) d /= 100;
            return d;
        } catch (NumberFormatException e) {
            throw new ParseException("Unparseable number: \"" + source + "\"", 0);
        }
    }

    public void setMaximumFractionDigits(int n) {
        maxFrac = Math.max(0, n);
        if (minFrac > maxFrac) minFrac = maxFrac;
    }

    public void setMinimumFractionDigits(int n) {
        minFrac = Math.max(0, n);
        if (maxFrac < minFrac) maxFrac = minFrac;
    }

    public void setMaximumIntegerDigits(int n) {
        maxInt = Math.max(0, n);
        if (minInt > maxInt) minInt = maxInt;
    }

    public void setMinimumIntegerDigits(int n) {
        minInt = Math.max(0, n);
        if (maxInt < minInt) maxInt = minInt;
    }

    public int getMaximumFractionDigits() { return maxFrac; }

    public int getMinimumFractionDigits() { return minFrac; }

    public void setGroupingUsed(boolean v) { useGrouping = v; }

    public void setGroupingSize(int n) { grouping = n; }

    public void setRoundingMode(RoundingMode mode) { rounding = mode; }

    public void setParseIntegerOnly(boolean v) { parseIntegerOnly = v; }

    public void setDecimalSeparatorAlwaysShown(boolean v) { alwaysShowDecimal = v; }

    public void setPositivePrefix(String p) { prefix = p; }

    public void setNegativePrefix(String p) { negPrefix = p; if (negSuffix == null) negSuffix = ""; }
}

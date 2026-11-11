package java.text;

import java.math.RoundingMode;
import java.util.Locale;

public abstract class NumberFormat extends Format {
    protected NumberFormat() {}

    public static NumberFormat getInstance() { return new DecimalFormat("#,##0.###"); }

    public static NumberFormat getInstance(Locale l) { return getInstance(); }

    public static NumberFormat getNumberInstance() { return getInstance(); }

    public static NumberFormat getNumberInstance(Locale l) { return getInstance(); }

    public static NumberFormat getIntegerInstance() {
        DecimalFormat f = new DecimalFormat("#,##0");
        f.setParseIntegerOnly(true);
        return f;
    }

    public static NumberFormat getIntegerInstance(Locale l) { return getIntegerInstance(); }

    public static NumberFormat getCurrencyInstance() { return new DecimalFormat("$#,##0.00"); }

    public static NumberFormat getCurrencyInstance(Locale l) { return getCurrencyInstance(); }

    public static NumberFormat getPercentInstance() { return new DecimalFormat("#,##0%"); }

    public static NumberFormat getPercentInstance(Locale l) { return getPercentInstance(); }

    public final String format(double number) { return format(number, new StringBuffer(), null).toString(); }

    public final String format(long number) { return format(number, new StringBuffer(), null).toString(); }

    public abstract StringBuffer format(double number, StringBuffer toAppendTo, FieldPosition pos);

    public abstract StringBuffer format(long number, StringBuffer toAppendTo, FieldPosition pos);

    public StringBuffer format(Object number, StringBuffer toAppendTo, FieldPosition pos) {
        if (number instanceof Long || number instanceof Integer || number instanceof Short || number instanceof Byte)
            return format(((Number) number).longValue(), toAppendTo, pos);
        if (number instanceof java.math.BigDecimal) return ((DecimalFormat) this).formatBig((java.math.BigDecimal) number, toAppendTo);
        if (number instanceof java.math.BigInteger) return ((DecimalFormat) this).formatBig(new java.math.BigDecimal((java.math.BigInteger) number), toAppendTo);
        if (number instanceof Number) return format(((Number) number).doubleValue(), toAppendTo, pos);
        throw new IllegalArgumentException("Cannot format given Object as a Number");
    }

    public abstract Number parse(String source) throws ParseException;

    public abstract void setMaximumFractionDigits(int n);

    public abstract void setMinimumFractionDigits(int n);

    public abstract void setMaximumIntegerDigits(int n);

    public abstract void setMinimumIntegerDigits(int n);

    public abstract int getMaximumFractionDigits();

    public abstract int getMinimumFractionDigits();

    public abstract void setGroupingUsed(boolean v);

    public abstract void setRoundingMode(RoundingMode mode);

    public abstract void setParseIntegerOnly(boolean v);
}

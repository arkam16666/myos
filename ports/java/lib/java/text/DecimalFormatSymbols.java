package java.text;

public class DecimalFormatSymbols implements java.io.Serializable {
    public DecimalFormatSymbols() {}

    public DecimalFormatSymbols(java.util.Locale l) {}

    public static DecimalFormatSymbols getInstance() { return new DecimalFormatSymbols(); }

    public static DecimalFormatSymbols getInstance(java.util.Locale l) { return new DecimalFormatSymbols(); }

    public char getDecimalSeparator() { return '.'; }

    public char getGroupingSeparator() { return ','; }
}

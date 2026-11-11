package java.util;

public class UnknownFormatConversionException extends IllegalFormatException {
    private String s;

    public UnknownFormatConversionException(String s) {
        super("Conversion = '" + s + "'");
        this.s = s;
    }

    public String getConversion() { return s; }
}

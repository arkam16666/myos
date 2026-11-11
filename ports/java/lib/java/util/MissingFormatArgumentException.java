package java.util;

public class MissingFormatArgumentException extends IllegalFormatException {
    public MissingFormatArgumentException(String s) { super("Format specifier '" + s + "'"); }
}

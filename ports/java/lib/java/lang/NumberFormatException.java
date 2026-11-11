package java.lang;

public class NumberFormatException extends IllegalArgumentException {
    public NumberFormatException() { super(); }

    public NumberFormatException(String s) { super(s); }

    static NumberFormatException forInputString(String s, int radix) {
        return new NumberFormatException("For input string: \"" + s + "\"" + (radix == 10 ? "" : " under radix " + radix));
    }
}

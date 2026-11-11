package java.lang;

public final class Character implements java.io.Serializable, Comparable<Character> {
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '\uffff';
    public static final char MIN_HIGH_SURROGATE = '\uD800';
    public static final char MAX_HIGH_SURROGATE = '\uDBFF';
    public static final char MIN_LOW_SURROGATE = '\uDC00';
    public static final char MAX_LOW_SURROGATE = '\uDFFF';
    public static final char MIN_SURROGATE = '\uD800';
    public static final char MAX_SURROGATE = '\uDFFF';
    public static final int MIN_SUPPLEMENTARY_CODE_POINT = 0x010000;
    public static final int MIN_CODE_POINT = 0;
    public static final int MAX_CODE_POINT = 0x10FFFF;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    @SuppressWarnings("unchecked")
    public static final Class<Character> TYPE = (Class<Character>) Class.getPrimitiveClass("char");
    private static final Character[] cache = new Character[128];

    private final char value;

    public Character(char value) { this.value = value; }

    public static Character valueOf(char c) {
        if (c < 128) {
            Character ch = cache[c];
            if (ch == null) cache[c] = ch = new Character(c);
            return ch;
        }
        return new Character(c);
    }

    public char charValue() { return value; }

    public int hashCode() { return value; }

    public static int hashCode(char value) { return value; }

    public boolean equals(Object obj) { return obj instanceof Character && value == ((Character) obj).value; }

    public String toString() { return String.valueOf(value); }

    public static String toString(char c) { return String.valueOf(c); }

    public static String toString(int codePoint) { return new String(toChars(codePoint)); }

    public int compareTo(Character another) { return value - another.value; }

    public static int compare(char x, char y) { return x - y; }

    public static boolean isValidCodePoint(int codePoint) { return codePoint >= 0 && codePoint <= MAX_CODE_POINT; }

    public static boolean isBmpCodePoint(int codePoint) { return codePoint >>> 16 == 0; }

    public static boolean isSupplementaryCodePoint(int codePoint) { return codePoint >= 0x10000 && codePoint <= MAX_CODE_POINT; }

    public static boolean isHighSurrogate(char ch) { return ch >= '\uD800' && ch <= '\uDBFF'; }

    public static boolean isLowSurrogate(char ch) { return ch >= '\uDC00' && ch <= '\uDFFF'; }

    public static boolean isSurrogate(char ch) { return ch >= '\uD800' && ch <= '\uDFFF'; }

    public static boolean isSurrogatePair(char high, char low) { return isHighSurrogate(high) && isLowSurrogate(low); }

    public static int charCount(int codePoint) { return codePoint >= 0x10000 ? 2 : 1; }

    public static int toCodePoint(char high, char low) { return ((high - 0xD800) << 10) + (low - 0xDC00) + 0x10000; }

    public static int codePointAt(CharSequence seq, int index) {
        char c1 = seq.charAt(index);
        if (isHighSurrogate(c1) && ++index < seq.length()) {
            char c2 = seq.charAt(index);
            if (isLowSurrogate(c2)) return toCodePoint(c1, c2);
        }
        return c1;
    }

    public static int codePointAt(char[] a, int index) {
        char c1 = a[index];
        if (isHighSurrogate(c1) && ++index < a.length) {
            char c2 = a[index];
            if (isLowSurrogate(c2)) return toCodePoint(c1, c2);
        }
        return c1;
    }

    public static char[] toChars(int codePoint) {
        if (codePoint < 0x10000) return new char[] { (char) codePoint };
        int c = codePoint - 0x10000;
        return new char[] { (char) (0xD800 | (c >> 10)), (char) (0xDC00 | (c & 0x3FF)) };
    }

    public static char highSurrogate(int codePoint) { return (char) ((codePoint >>> 10) + (0xD800 - (0x10000 >>> 10))); }

    public static char lowSurrogate(int codePoint) { return (char) ((codePoint & 0x3ff) + 0xDC00); }

    /* letters: ASCII, Latin-1, Latin Extended, Greek, Cyrillic, Hebrew, Arabic, Thai, CJK, kana, Hangul */
    public static boolean isLetter(int c) {
        if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (c < 256) return c == 0xAA || c == 0xB5 || c == 0xBA || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
        if (c < 0x2B0) return true;
        if (c >= 0x370 && c < 0x3FF) return c != 0x37E && c != 0x387 && c != 0x375;
        if (c >= 0x400 && c < 0x530) return c < 0x482 || c >= 0x48A;
        if (c >= 0x5D0 && c <= 0x5EA) return true;
        if (c >= 0x620 && c <= 0x64A) return true;
        if (c >= 0xE01 && c <= 0xE30) return true;
        if (c == 0xE32 || c == 0xE33 || (c >= 0xE40 && c <= 0xE46)) return true;
        if (c >= 0x1E00 && c < 0x2000) return true;
        if (c >= 0x3041 && c <= 0x30FF) return c != 0x30A0 && c != 0x30FB;
        if (c >= 0x3400 && c <= 0x9FFF) return true;
        if (c >= 0xAC00 && c <= 0xD7A3) return true;
        if (c >= 0xF900 && c <= 0xFAFF) return true;
        if (c >= 0xFF21 && c <= 0xFF3A) return true;
        if (c >= 0xFF41 && c <= 0xFF5A) return true;
        return c >= 0x20000 && c <= 0x3134F;
    }

    public static boolean isLetter(char ch) { return isLetter((int) ch); }

    public static boolean isDigit(int c) {
        if (c < 128) return c >= '0' && c <= '9';
        return (c >= 0x660 && c <= 0x669) || (c >= 0x6F0 && c <= 0x6F9) || (c >= 0x966 && c <= 0x96F)
                || (c >= 0xE50 && c <= 0xE59) || (c >= 0xFF10 && c <= 0xFF19);
    }

    public static boolean isDigit(char ch) { return isDigit((int) ch); }

    public static boolean isLetterOrDigit(int c) { return isLetter(c) || isDigit(c); }

    public static boolean isLetterOrDigit(char ch) { return isLetterOrDigit((int) ch); }

    public static boolean isAlphabetic(int c) { return isLetter(c); }

    public static boolean isIdeographic(int c) { return c >= 0x3400 && c <= 0x9FFF; }

    public static boolean isUpperCase(int c) {
        if (c < 128) return c >= 'A' && c <= 'Z';
        return toLowerCase(c) != c || (c >= 0x2160 && c <= 0x216F);
    }

    public static boolean isUpperCase(char ch) { return isUpperCase((int) ch); }

    public static boolean isLowerCase(int c) {
        if (c < 128) return c >= 'a' && c <= 'z';
        if (c == 0xAA || c == 0xBA || c == 0xDF) return true;
        return toUpperCase(c) != c;
    }

    public static boolean isLowerCase(char ch) { return isLowerCase((int) ch); }

    public static boolean isTitleCase(char ch) { return false; }

    public static boolean isWhitespace(int c) {
        return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0x1680
                || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A) || c == 0x2028 || c == 0x2029
                || c == 0x205F || c == 0x3000;
    }

    public static boolean isWhitespace(char ch) { return isWhitespace((int) ch); }

    public static boolean isSpaceChar(int c) {
        return c == ' ' || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029
                || c == 0x202F || c == 0x205F || c == 0x3000;
    }

    public static boolean isSpaceChar(char ch) { return isSpaceChar((int) ch); }

    public static boolean isISOControl(int c) { return c <= 0x1F || (c >= 0x7F && c <= 0x9F); }

    public static boolean isISOControl(char ch) { return isISOControl((int) ch); }

    public static boolean isJavaIdentifierStart(int c) { return isLetter(c) || c == '_' || c == '$'; }

    public static boolean isJavaIdentifierStart(char ch) { return isJavaIdentifierStart((int) ch); }

    public static boolean isJavaIdentifierPart(int c) { return isLetterOrDigit(c) || c == '_' || c == '$'; }

    public static boolean isJavaIdentifierPart(char ch) { return isJavaIdentifierPart((int) ch); }

    public static boolean isDefined(int c) { return c >= 0 && c <= MAX_CODE_POINT; }

    public static int toLowerCase(int c) {
        if (c < 128) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
        if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
        if (c >= 0x100 && c < 0x138 && c != 0x130) return c | 1;
        if (c >= 0x139 && c < 0x149) return (c & 1) == 1 ? c + 1 : c;
        if (c >= 0x14A && c < 0x178) return c | 1;
        if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
        if (c >= 0x410 && c <= 0x42F) return c + 32;
        if (c >= 0x400 && c <= 0x40F) return c + 80;
        if (c >= 0x460 && c < 0x482) return c | 1;
        if (c >= 0x1E00 && c < 0x1E96) return c | 1;
        if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
        return c;
    }

    public static char toLowerCase(char ch) { return (char) toLowerCase((int) ch); }

    public static int toUpperCase(int c) {
        if (c < 128) return (c >= 'a' && c <= 'z') ? c - 32 : c;
        if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
        if (c == 0xFF) return 0x178;
        if (c == 0xB5) return 0x39C;
        if (c >= 0x100 && c < 0x138 && c != 0x131) return c & ~1;
        if (c >= 0x139 && c < 0x149) return (c & 1) == 0 ? c - 1 : c;
        if (c >= 0x14A && c < 0x178) return c & ~1;
        if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
        if (c == 0x3C2) return 0x3A3;
        if (c >= 0x430 && c <= 0x44F) return c - 32;
        if (c >= 0x450 && c <= 0x45F) return c - 80;
        if (c >= 0x460 && c < 0x482) return c & ~1;
        if (c >= 0x1E00 && c < 0x1E96) return c & ~1;
        if (c >= 0xFF41 && c <= 0xFF5A) return c - 32;
        return c;
    }

    public static char toUpperCase(char ch) { return (char) toUpperCase((int) ch); }

    public static char toTitleCase(char ch) { return toUpperCase(ch); }

    public static int digit(int c, int radix) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else if (c >= 0xFF10 && c <= 0xFF19) d = c - 0xFF10;
        else if (c >= 0xE50 && c <= 0xE59) d = c - 0xE50;
        else if (c >= 0x660 && c <= 0x669) d = c - 0x660;
        else return -1;
        return d < radix ? d : -1;
    }

    public static int digit(char ch, int radix) { return digit((int) ch, radix); }

    public static int getNumericValue(int c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'z') return c - 'a' + 10;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        int d = digit(c, 10);
        return d;
    }

    public static int getNumericValue(char ch) { return getNumericValue((int) ch); }

    public static char forDigit(int digit, int radix) {
        if (digit >= radix || digit < 0 || radix < 2 || radix > 36) return '\0';
        return (char) (digit < 10 ? '0' + digit : 'a' - 10 + digit);
    }

    public static char reverseBytes(char ch) { return (char) (((ch & 0xFF00) >> 8) | (ch << 8)); }

    public static String getName(int codePoint) { return null; }
}

package java.util.regex;

public class PatternSyntaxException extends IllegalArgumentException {
    private final String desc, pattern;
    private final int index;

    public PatternSyntaxException(String desc, String regex, int index) {
        super(desc + (index >= 0 ? " near index " + index : "") + "\n" + regex);
        this.desc = desc;
        this.pattern = regex;
        this.index = index;
    }

    public String getDescription() { return desc; }

    public String getPattern() { return pattern; }

    public int getIndex() { return index; }
}

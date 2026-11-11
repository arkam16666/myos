package java.util.regex;

public final class Matcher implements MatchResult {
    private Pattern pattern;
    CharSequence text;
    int from, to;               /* the region */
    int[] groups, tmpStart, loopCount, loopStart;
    int last;
    boolean anchorEnd;
    private int first = -1, lastEnd = 0;     /* the current match */
    private boolean lastEmpty;
    private int appendPos;
    private boolean matched;

    Matcher(Pattern p, CharSequence text) {
        pattern = p;
        this.text = text;
        groups = new int[(p.groupCount + 1) * 2];
        tmpStart = new int[p.groupCount + 1];
        loopCount = new int[p.loopCount + 1];
        loopStart = new int[p.loopCount + 1];
        reset();
    }

    public Pattern pattern() { return pattern; }

    public Matcher reset() {
        from = 0;
        to = text.length();
        first = -1;
        lastEnd = 0;
        lastEmpty = false;
        appendPos = 0;
        matched = false;
        for (int i = 0; i < groups.length; i++) groups[i] = -1;
        return this;
    }

    public Matcher reset(CharSequence input) {
        text = input;
        return reset();
    }

    public Matcher region(int start, int end) {
        if (start < 0 || end > text.length() || start > end) throw new IndexOutOfBoundsException();
        reset();
        from = start;
        to = end;
        lastEnd = start;
        return this;
    }

    public int regionStart() { return from; }

    public int regionEnd() { return to; }

    public Matcher usePattern(Pattern p) {
        pattern = p;
        groups = new int[(p.groupCount + 1) * 2];
        tmpStart = new int[p.groupCount + 1];
        loopCount = new int[p.loopCount + 1];
        loopStart = new int[p.loopCount + 1];
        for (int i = 0; i < groups.length; i++) groups[i] = -1;
        return this;
    }

    private boolean tryAt(int i, boolean toEnd) {
        for (int k = 0; k < groups.length; k++) groups[k] = -1;
        anchorEnd = toEnd;
        if (pattern.root.match(this, i)) {
            first = i;
            groups[0] = i;
            groups[1] = last;
            matched = true;
            return true;
        }
        return false;
    }

    public boolean matches() {
        matched = false;
        boolean ok = tryAt(from, true);
        if (ok) {
            lastEnd = last;
            lastEmpty = first == last;
        }
        return ok;
    }

    public boolean lookingAt() {
        matched = false;
        boolean ok = tryAt(from, false);
        if (ok) {
            lastEnd = last;
            lastEmpty = first == last;
        }
        return ok;
    }

    public boolean find() {
        int start = lastEnd;
        if (lastEmpty) start++;
        return search(start);
    }

    public boolean find(int start) {
        if (start < 0 || start > text.length()) throw new IndexOutOfBoundsException("Illegal start index");
        reset();
        return search(start);
    }

    private boolean search(int start) {
        matched = false;
        for (int i = start; i <= to; i++) {
            if (tryAt(i, false)) {
                lastEnd = last;
                lastEmpty = first == last;
                return true;
            }
        }
        lastEnd = to + 1;
        lastEmpty = false;
        return false;
    }

    public boolean hitEnd() { return false; }

    private void check() {
        if (!matched) throw new IllegalStateException("No match found");
    }

    public int start() {
        check();
        return groups[0];
    }

    public int start(int group) {
        check();
        if (group < 0 || group > groupCount()) throw new IndexOutOfBoundsException("No group " + group);
        return groups[2 * group];
    }

    public int start(String name) { return start(groupIndex(name)); }

    public int end() {
        check();
        return groups[1];
    }

    public int end(int group) {
        check();
        if (group < 0 || group > groupCount()) throw new IndexOutOfBoundsException("No group " + group);
        return groups[2 * group + 1];
    }

    public int end(String name) { return end(groupIndex(name)); }

    public String group() { return group(0); }

    public String group(int group) {
        check();
        if (group < 0 || group > groupCount()) throw new IndexOutOfBoundsException("No group " + group);
        int s = groups[2 * group], e = groups[2 * group + 1];
        if (s < 0 || e < 0) return null;
        return text.subSequence(s, e).toString();
    }

    private int groupIndex(String name) {
        Integer g = pattern.names.get(name);
        if (g == null) throw new IllegalArgumentException("No group with name <" + name + ">");
        return g;
    }

    public String group(String name) { return group(groupIndex(name)); }

    public int groupCount() { return pattern.groupCount; }

    public MatchResult toMatchResult() {
        check();
        final String[] g = new String[groupCount() + 1];
        final int[] pos = groups.clone();
        for (int i = 0; i < g.length; i++) g[i] = group(i);
        return new MatchResult() {
            public int start() { return pos[0]; }

            public int start(int group) { return pos[2 * group]; }

            public int end() { return pos[1]; }

            public int end(int group) { return pos[2 * group + 1]; }

            public String group() { return g[0]; }

            public String group(int group) { return g[group]; }

            public int groupCount() { return g.length - 1; }
        };
    }

    public java.util.stream.Stream<MatchResult> results() {
        java.util.ArrayList<MatchResult> l = new java.util.ArrayList<MatchResult>();
        reset();
        while (find()) l.add(toMatchResult());
        return l.stream();
    }

    public static String quoteReplacement(String s) {
        if (s.indexOf('\\') < 0 && s.indexOf('$') < 0) return s;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\' || c == '$') sb.append('\\');
            sb.append(c);
        }
        return sb.toString();
    }

    private void appendExpanded(StringBuilder sb, String replacement) {
        for (int i = 0; i < replacement.length(); i++) {
            char c = replacement.charAt(i);
            if (c == '\\') {
                i++;
                if (i >= replacement.length()) throw new IllegalArgumentException("character to be escaped is missing");
                sb.append(replacement.charAt(i));
            } else if (c == '$') {
                i++;
                if (i >= replacement.length()) throw new IllegalArgumentException("Illegal group reference: group index is missing");
                if (replacement.charAt(i) == '{') {
                    int e = replacement.indexOf('}', i);
                    String name = replacement.substring(i + 1, e);
                    String g = group(name);
                    if (g != null) sb.append(g);
                    i = e;
                } else {
                    int g = replacement.charAt(i) - '0';
                    if (g < 0 || g > 9) throw new IllegalArgumentException("Illegal group reference");
                    while (i + 1 < replacement.length() && Character.isDigit(replacement.charAt(i + 1))
                            && g * 10 + (replacement.charAt(i + 1) - '0') <= groupCount()) {
                        g = g * 10 + (replacement.charAt(++i) - '0');
                    }
                    if (g > groupCount()) throw new IndexOutOfBoundsException("No group " + g);
                    String s = group(g);
                    if (s != null) sb.append(s);
                }
            } else sb.append(c);
        }
    }

    public Matcher appendReplacement(StringBuilder sb, String replacement) {
        check();
        sb.append(text, appendPos, start());
        appendExpanded(sb, replacement);
        appendPos = end();
        return this;
    }

    public Matcher appendReplacement(StringBuffer sb, String replacement) {
        StringBuilder t = new StringBuilder();
        appendReplacement(t, replacement);
        sb.append(t);
        return this;
    }

    public StringBuilder appendTail(StringBuilder sb) {
        sb.append(text, appendPos, text.length());
        return sb;
    }

    public StringBuffer appendTail(StringBuffer sb) {
        sb.append(text, appendPos, text.length());
        return sb;
    }

    public String replaceAll(String replacement) {
        reset();
        if (!find()) return text.toString();
        StringBuilder sb = new StringBuilder();
        do {
            appendReplacement(sb, replacement);
        } while (find());
        return appendTail(sb).toString();
    }

    public String replaceAll(java.util.function.Function<MatchResult, String> replacer) {
        reset();
        if (!find()) return text.toString();
        StringBuilder sb = new StringBuilder();
        do {
            appendReplacement(sb, replacer.apply(toMatchResult()));
        } while (find());
        return appendTail(sb).toString();
    }

    public String replaceFirst(String replacement) {
        reset();
        if (!find()) return text.toString();
        StringBuilder sb = new StringBuilder();
        appendReplacement(sb, replacement);
        return appendTail(sb).toString();
    }
}

package java.util.regex;

import java.util.ArrayList;
import java.util.HashMap;

/** A backtracking regular expression engine (a subset of java.util.regex). */
public final class Pattern implements java.io.Serializable {
    public static final int UNIX_LINES = 0x01;
    public static final int CASE_INSENSITIVE = 0x02;
    public static final int COMMENTS = 0x04;
    public static final int MULTILINE = 0x08;
    public static final int LITERAL = 0x10;
    public static final int DOTALL = 0x20;
    public static final int UNICODE_CASE = 0x40;

    private final String pattern;
    private int flags;
    Node root;
    int groupCount, loopCount;
    HashMap<String, Integer> names = new HashMap<String, Integer>();
    private int at;

    private Pattern(String p, int f) {
        pattern = p;
        flags = f;
        if ((f & LITERAL) != 0) {
            Node n = new Slice(p, false);
            n.next = new Accept();
            root = n;
        } else {
            Node[] seq = parseAlt();
            if (at < p.length()) throw error(p.charAt(at) == ')' ? "Unmatched closing ')'" : "Unexpected character");
            seq[1].next = new Accept();
            root = seq[0];
        }
    }

    public static Pattern compile(String regex) { return new Pattern(regex, 0); }

    public static Pattern compile(String regex, int flags) { return new Pattern(regex, flags); }

    public String pattern() { return pattern; }

    public String toString() { return pattern; }

    public int flags() { return flags; }

    public Matcher matcher(CharSequence input) { return new Matcher(this, input); }

    public static boolean matches(String regex, CharSequence input) { return compile(regex).matcher(input).matches(); }

    public static String quote(String s) {
        int e = s.indexOf("\\E");
        if (e < 0) return "\\Q" + s + "\\E";
        StringBuilder sb = new StringBuilder("\\Q");
        int cur = 0;
        while ((e = s.indexOf("\\E", cur)) >= 0) {
            sb.append(s, cur, e).append("\\E\\\\E\\Q");
            cur = e + 2;
        }
        return sb.append(s, cur, s.length()).append("\\E").toString();
    }

    public java.util.function.Predicate<String> asPredicate() {
        return new java.util.function.Predicate<String>() {
            public boolean test(String s) { return matcher(s).find(); }
        };
    }

    public java.util.function.Predicate<String> asMatchPredicate() {
        return new java.util.function.Predicate<String>() {
            public boolean test(String s) { return matcher(s).matches(); }
        };
    }

    public String[] split(CharSequence input) { return split(input, 0); }

    public String[] split(CharSequence input, int limit) {
        int index = 0;
        boolean matchLimited = limit > 0;
        ArrayList<String> list = new ArrayList<String>();
        Matcher m = matcher(input);
        while (m.find()) {
            if (!matchLimited || list.size() < limit - 1) {
                if (index == 0 && index == m.start() && m.start() == m.end()) continue;
                list.add(input.subSequence(index, m.start()).toString());
                index = m.end();
            } else if (list.size() == limit - 1) {
                list.add(input.subSequence(index, input.length()).toString());
                index = m.end();
            }
        }
        if (index == 0) return new String[] { input.toString() };
        if (!matchLimited || list.size() < limit) list.add(input.subSequence(index, input.length()).toString());
        int size = list.size();
        if (limit == 0) while (size > 0 && list.get(size - 1).isEmpty()) size--;
        String[] r = new String[size];
        for (int i = 0; i < size; i++) r[i] = list.get(i);
        return r;
    }

    public java.util.stream.Stream<String> splitAsStream(CharSequence input) {
        return java.util.Arrays.stream(split(input));
    }

    /* ------------------------------------------------------------ parser */
    private PatternSyntaxException error(String msg) { return new PatternSyntaxException(msg, pattern, at); }

    private boolean more() { return at < pattern.length(); }

    private char peek() { return pattern.charAt(at); }

    private boolean ci() { return (flags & CASE_INSENSITIVE) != 0; }

    /* returns {first, last} of a node chain */
    private Node[] parseAlt() {
        Node[] first = parseSeq();
        if (!more() || peek() != '|') return first;
        Branch b = new Branch();
        BranchEnd end = new BranchEnd();
        b.alts.add(first[0]);
        first[1].next = end;
        while (more() && peek() == '|') {
            at++;
            Node[] s = parseSeq();
            b.alts.add(s[0]);
            s[1].next = end;
        }
        return new Node[] { b, end };
    }

    private Node[] parseSeq() {
        Node head = new Empty(), tail = head;
        while (more()) {
            char c = peek();
            if (c == '|' || c == ')') break;
            Node[] atom = parseAtom();
            if (atom == null) continue;
            atom = parseQuant(atom);
            tail.next = atom[0];
            tail = atom[1];
        }
        return new Node[] { head, tail };
    }

    private Node[] parseQuant(Node[] atom) {
        if (!more()) return atom;
        char c = peek();
        int min, max;
        if (c == '*') { min = 0; max = Integer.MAX_VALUE; at++; }
        else if (c == '+') { min = 1; max = Integer.MAX_VALUE; at++; }
        else if (c == '?') { min = 0; max = 1; at++; }
        else if (c == '{') {
            int save = at;
            at++;
            int a = number();
            if (a < 0) { at = save; return atom; }
            int b = a;
            if (more() && peek() == ',') {
                at++;
                b = more() && Character.isDigit(peek()) ? number() : Integer.MAX_VALUE;
            }
            if (!more() || peek() != '}') throw error("Unclosed counted closure");
            at++;
            min = a;
            max = b;
            if (max < min) throw error("Illegal repetition range");
        } else return atom;
        boolean lazy = false;
        if (more() && peek() == '?') { lazy = true; at++; }
        else if (more() && peek() == '+') at++;      /* possessive: treated as greedy */
        if (atom[0] instanceof Single && atom[0] == atom[1]) {
            Single s = (Single) atom[0];
            Node q = new CharRepeat(s.pred, min, max, lazy);
            return parseQuant(new Node[] { q, q });
        }
        Repeat r = new Repeat(loopCount++, min, max, lazy);
        RepeatTail t = new RepeatTail(r);
        r.body = atom[0];
        atom[1].next = t;
        return parseQuant(new Node[] { r, r });
    }

    private int number() {
        int start = at;
        while (more() && Character.isDigit(peek())) at++;
        if (at == start) return -1;
        return Integer.parseInt(pattern.substring(start, at));
    }

    private Node[] one(Node n) { return new Node[] { n, n }; }

    private Node[] parseAtom() {
        char c = pattern.charAt(at++);
        switch (c) {
            case '(':
                return parseGroup();
            case '[': {
                CharPred p = parseClass();
                return one(new Single(p));
            }
            case '.':
                return one(new Single((flags & DOTALL) != 0 ? CharPred.ANY : CharPred.DOT));
            case '^':
                return one(new Caret((flags & MULTILINE) != 0));
            case '$':
                return one(new Dollar((flags & MULTILINE) != 0));
            case '\\':
                return parseEscapeAtom();
            case '*': case '+': case '?':
                throw error("Dangling meta character '" + c + "'");
            default:
                if ((flags & COMMENTS) != 0) {
                    if (Character.isWhitespace(c)) return null;
                    if (c == '#') {
                        while (more() && peek() != '\n') at++;
                        return null;
                    }
                }
                return one(new Single(CharPred.literal(c, ci())));
        }
    }

    private Node[] parseGroup() {
        int capture = -1;
        int kind = 0;   /* 0 group, 1 lookahead, 2 negative lookahead, 3 lookbehind, 4 negative lookbehind */
        if (more() && peek() == '?') {
            at++;
            if (!more()) throw error("Unknown group type");
            char t = pattern.charAt(at++);
            if (t == ':') {
                /* non-capturing */
            } else if (t == '=') kind = 1;
            else if (t == '!') kind = 2;
            else if (t == '<' && more() && (peek() == '=' || peek() == '!')) {
                kind = peek() == '=' ? 3 : 4;
                at++;
            } else if (t == '<') {
                int s = at;
                while (more() && peek() != '>') at++;
                if (!more()) throw error("named capturing group is missing trailing '>'");
                String name = pattern.substring(s, at++);
                capture = ++groupCount;
                names.put(name, capture);
            } else if (t == '>') {
                /* atomic group: treated as a plain group */
            } else {
                /* inline flags (?i) (?s) (?m) (?x) (?-i) ... optionally followed by :X) */
                at--;
                boolean on = true;
                while (more() && peek() != ')' && peek() != ':') {
                    char f = pattern.charAt(at++);
                    int bit = f == 'i' ? CASE_INSENSITIVE : f == 's' ? DOTALL : f == 'm' ? MULTILINE
                            : f == 'x' ? COMMENTS : f == 'u' ? UNICODE_CASE : f == 'd' ? UNIX_LINES : -1;
                    if (f == '-') on = false;
                    else if (bit < 0) throw error("Unknown inline modifier");
                    else if (on) flags |= bit;
                    else flags &= ~bit;
                }
                if (!more()) throw error("Unclosed group");
                if (peek() == ')') {
                    at++;
                    return null;
                }
                at++;
            }
        } else capture = ++groupCount;
        Node[] body = parseAlt();
        if (!more() || peek() != ')') throw error("Unclosed group");
        at++;
        if (kind == 1 || kind == 2) {
            body[1].next = new Accept();
            return one(new Look(body[0], kind == 2));
        }
        if (kind == 3 || kind == 4) {
            LookBehindEnd end = new LookBehindEnd();
            body[1].next = end;
            return one(new LookBehind(body[0], end, kind == 4));
        }
        if (capture < 0) return body;
        GroupHead h = new GroupHead(capture);
        GroupTail t = new GroupTail(capture);
        h.next = body[0];
        body[1].next = t;
        return new Node[] { h, t };
    }

    private Node[] parseEscapeAtom() {
        if (!more()) throw error("Unexpected internal error");
        char c = peek();
        if (c >= '1' && c <= '9') {
            at++;
            int g = c - '0';
            while (more() && Character.isDigit(peek()) && g * 10 + (peek() - '0') <= groupCount) g = g * 10 + (pattern.charAt(at++) - '0');
            return one(new BackRef(g, ci()));
        }
        switch (c) {
            case 'b': at++; return one(new Bound(false));
            case 'B': at++; return one(new Bound(true));
            case 'A': at++; return one(new Caret(false));
            case 'z': at++; return one(new End(false));
            case 'Z': at++; return one(new End(true));
            case 'G': at++; return one(new Empty());
            case 'R': at++; {
                Node[] crlf = parseLiteral("\r\n");
                Branch b = new Branch();
                BranchEnd end = new BranchEnd();
                crlf[1].next = end;
                b.alts.add(crlf[0]);
                Node lf = new Single(new CharPred.Set("\n\u000B\u000C\r\u0085\u2028\u2029"));
                lf.next = end;
                b.alts.add(lf);
                return new Node[] { b, end };
            }
            case 'k': {
                at++;
                if (!more() || peek() != '<') throw error("\\k is not followed by '<' for named capturing group");
                int s = ++at;
                while (more() && peek() != '>') at++;
                String name = pattern.substring(s, at++);
                Integer g = names.get(name);
                if (g == null) throw error("named capturing group <" + name + "> does not exist");
                return one(new BackRef(g, ci()));
            }
            case 'Q': {
                at++;
                int e = pattern.indexOf("\\E", at);
                String lit = e < 0 ? pattern.substring(at) : pattern.substring(at, e);
                at = e < 0 ? pattern.length() : e + 2;
                if (lit.isEmpty()) return null;
                return parseLiteral(lit);
            }
        }
        return one(new Single(parseEscapeClass()));
    }

    private Node[] parseLiteral(String lit) {
        Node n = new Slice(lit, ci());
        return one(n);
    }

    /* after '\': a character or a predefined class */
    private CharPred parseEscapeClass() {
        char c = pattern.charAt(at++);
        switch (c) {
            case 'd': return CharPred.DIGIT;
            case 'D': return CharPred.not(CharPred.DIGIT);
            case 'w': return CharPred.WORD;
            case 'W': return CharPred.not(CharPred.WORD);
            case 's': return CharPred.SPACE;
            case 'S': return CharPred.not(CharPred.SPACE);
            case 'h': return new CharPred.Set(" \t\u00a0\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000");
            case 'H': return CharPred.not(new CharPred.Set(" \t\u00a0\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000"));
            case 'v': return new CharPred.Set("\n\u000B\u000C\r\u0085\u2028\u2029");
            case 'p': case 'P': {
                String name;
                if (more() && peek() == '{') {
                    int e = pattern.indexOf('}', at);
                    if (e < 0) throw error("Unclosed character family");
                    name = pattern.substring(at + 1, e);
                    at = e + 1;
                } else name = String.valueOf(pattern.charAt(at++));
                CharPred p = CharPred.property(name);
                if (p == null) throw error("Unknown character property name {" + name + "}");
                return c == 'P' ? CharPred.not(p) : p;
            }
            case 't': return CharPred.literal('\t', false);
            case 'n': return CharPred.literal('\n', false);
            case 'r': return CharPred.literal('\r', false);
            case 'f': return CharPred.literal('\f', false);
            case 'a': return CharPred.literal('\u0007', false);
            case 'e': return CharPred.literal('\u001B', false);
            case '0': {
                int v = 0, k = 0;
                while (k < 3 && more() && peek() >= '0' && peek() <= '7') {
                    v = v * 8 + (pattern.charAt(at++) - '0');
                    k++;
                }
                return CharPred.literal((char) v, false);
            }
            case 'x': {
                int v;
                if (more() && peek() == '{') {
                    int e = pattern.indexOf('}', at);
                    v = Integer.parseInt(pattern.substring(at + 1, e), 16);
                    at = e + 1;
                } else {
                    v = Integer.parseInt(pattern.substring(at, at + 2), 16);
                    at += 2;
                }
                return CharPred.literal((char) v, ci());
            }
            case 'u': {
                int v = Integer.parseInt(pattern.substring(at, at + 4), 16);
                at += 4;
                return CharPred.literal((char) v, ci());
            }
            case 'c': return CharPred.literal((char) (pattern.charAt(at++) ^ 64), false);
            default:
                if (Character.isLetterOrDigit(c)) throw error("Illegal/unsupported escape sequence");
                return CharPred.literal(c, ci());
        }
    }

    /* after '[' */
    private CharPred parseClass() {
        boolean negate = false;
        if (more() && peek() == '^') {
            negate = true;
            at++;
        }
        CharPred.Union u = new CharPred.Union();
        boolean first = true;
        CharPred result = null;
        while (true) {
            if (!more()) throw error("Unclosed character class");
            char c = peek();
            if (c == ']' && !first) {
                at++;
                break;
            }
            first = false;
            if (c == '[') {
                at++;
                u.add(parseClass());
                continue;
            }
            if (c == '&' && at + 1 < pattern.length() && pattern.charAt(at + 1) == '&') {
                at += 2;
                CharPred left = u;
                CharPred right;
                if (more() && peek() == '[') {
                    at++;
                    right = parseClass();
                    CharPred inter = CharPred.and(left, right);
                    u = new CharPred.Union();
                    u.add(inter);
                } else {
                    /* the rest of this class is the right side */
                    at--;
                    StringBuilder rest = new StringBuilder("[");
                    right = parseRestAsClass();
                    CharPred inter = CharPred.and(left, right);
                    result = negate ? CharPred.not(inter) : inter;
                    return result;
                }
                continue;
            }
            CharPred single;
            int lo;
            if (c == '\\') {
                at++;
                int save = at;
                CharPred p = parseEscapeClass();
                if (!(p instanceof CharPred.Lit)) {
                    u.add(p);
                    continue;
                }
                lo = ((CharPred.Lit) p).c;
            } else {
                at++;
                lo = c;
            }
            if (more() && peek() == '-' && at + 1 < pattern.length() && pattern.charAt(at + 1) != ']') {
                at++;
                int hi;
                char d = pattern.charAt(at++);
                if (d == '\\') {
                    CharPred p = parseEscapeClass();
                    if (!(p instanceof CharPred.Lit)) throw error("Illegal character range");
                    hi = ((CharPred.Lit) p).c;
                } else if (d == '[') throw error("Illegal character range");
                else hi = d;
                if (hi < lo) throw error("Illegal character range");
                u.add(new CharPred.Range(lo, hi, ci()));
            } else u.add(new CharPred.Range(lo, lo, ci()));
        }
        return negate ? CharPred.not(u) : u;
    }

    private CharPred parseRestAsClass() {
        /* we are at the second '&'; parse "...]" as a class without a leading '[' */
        at++;
        return parseClass();
    }

    /* ------------------------------------------------------------ nodes */
    static abstract class Node {
        Node next;

        abstract boolean match(Matcher m, int i);
    }

    static final class Empty extends Node {
        boolean match(Matcher m, int i) { return next.match(m, i); }
    }

    static final class Accept extends Node {
        boolean match(Matcher m, int i) {
            if (m.anchorEnd && i != m.to) return false;
            m.last = i;
            return true;
        }
    }

    static final class Single extends Node {
        final CharPred pred;

        Single(CharPred p) { pred = p; }

        boolean match(Matcher m, int i) {
            return i < m.to && pred.is(m.text.charAt(i)) && next.match(m, i + 1);
        }
    }

    static final class Slice extends Node {
        final String s;
        final boolean ci;

        Slice(String s, boolean ci) {
            this.s = s;
            this.ci = ci;
        }

        boolean match(Matcher m, int i) {
            int n = s.length();
            if (i + n > m.to) return false;
            for (int k = 0; k < n; k++) {
                char a = m.text.charAt(i + k), b = s.charAt(k);
                if (a != b && !(ci && CharPred.eqIgnoreCase(a, b))) return false;
            }
            return next.match(m, i + n);
        }
    }

    static final class CharRepeat extends Node {
        final CharPred pred;
        final int min, max;
        final boolean lazy;

        CharRepeat(CharPred p, int min, int max, boolean lazy) {
            pred = p;
            this.min = min;
            this.max = max;
            this.lazy = lazy;
        }

        boolean match(Matcher m, int i) {
            int k = 0;
            while (k < min) {
                if (i + k >= m.to || !pred.is(m.text.charAt(i + k))) return false;
                k++;
            }
            if (lazy) {
                for (;;) {
                    if (next.match(m, i + k)) return true;
                    if (k >= max || i + k >= m.to || !pred.is(m.text.charAt(i + k))) return false;
                    k++;
                }
            }
            while (k < max && i + k < m.to && pred.is(m.text.charAt(i + k))) k++;
            for (; k >= min; k--) if (next.match(m, i + k)) return true;
            return false;
        }
    }

    static final class Repeat extends Node {
        final int id, min, max;
        final boolean lazy;
        Node body;

        Repeat(int id, int min, int max, boolean lazy) {
            this.id = id;
            this.min = min;
            this.max = max;
            this.lazy = lazy;
        }

        boolean match(Matcher m, int i) {
            int oc = m.loopCount[id], os = m.loopStart[id];
            m.loopCount[id] = 0;
            m.loopStart[id] = i;
            boolean ok = iterate(m, i);
            m.loopCount[id] = oc;
            m.loopStart[id] = os;
            return ok;
        }

        boolean iterate(Matcher m, int i) {
            int count = m.loopCount[id];
            if (lazy) {
                if (count >= min && next.match(m, i)) return true;
                return count < max && body.match(m, i);
            }
            if (count < max && body.match(m, i)) return true;
            return count >= min && next.match(m, i);
        }
    }

    static final class RepeatTail extends Node {
        final Repeat r;

        RepeatTail(Repeat r) { this.r = r; }

        boolean match(Matcher m, int j) {
            int id = r.id, count = m.loopCount[id], start = m.loopStart[id];
            if (j == start) return count + 1 >= r.min && r.next.match(m, j);    /* empty iteration: stop looping */
            m.loopCount[id] = count + 1;
            m.loopStart[id] = j;
            boolean ok = r.iterate(m, j);
            m.loopCount[id] = count;
            m.loopStart[id] = start;
            return ok;
        }
    }

    static final class Branch extends Node {
        final ArrayList<Node> alts = new ArrayList<Node>();

        boolean match(Matcher m, int i) {
            for (int k = 0; k < alts.size(); k++) if (alts.get(k).match(m, i)) return true;
            return false;
        }
    }

    static final class BranchEnd extends Node {
        boolean match(Matcher m, int i) { return next.match(m, i); }
    }

    static final class GroupHead extends Node {
        final int g;

        GroupHead(int g) { this.g = g; }

        boolean match(Matcher m, int i) {
            int old = m.tmpStart[g];
            m.tmpStart[g] = i;
            boolean ok = next.match(m, i);
            m.tmpStart[g] = old;
            return ok;
        }
    }

    static final class GroupTail extends Node {
        final int g;

        GroupTail(int g) { this.g = g; }

        boolean match(Matcher m, int i) {
            int os = m.groups[2 * g], oe = m.groups[2 * g + 1];
            m.groups[2 * g] = m.tmpStart[g];
            m.groups[2 * g + 1] = i;
            if (next.match(m, i)) return true;
            m.groups[2 * g] = os;
            m.groups[2 * g + 1] = oe;
            return false;
        }
    }

    static final class BackRef extends Node {
        final int g;
        final boolean ci;

        BackRef(int g, boolean ci) {
            this.g = g;
            this.ci = ci;
        }

        boolean match(Matcher m, int i) {
            if (g >= m.groups.length / 2) return false;
            int s = m.groups[2 * g], e = m.groups[2 * g + 1];
            if (s < 0) return false;
            int n = e - s;
            if (i + n > m.to) return false;
            for (int k = 0; k < n; k++) {
                char a = m.text.charAt(i + k), b = m.text.charAt(s + k);
                if (a != b && !(ci && CharPred.eqIgnoreCase(a, b))) return false;
            }
            return next.match(m, i + n);
        }
    }

    static final class Caret extends Node {
        final boolean multiline;

        Caret(boolean ml) { multiline = ml; }

        boolean match(Matcher m, int i) {
            if (i == m.from || (multiline && i > 0 && i < m.to && m.text.charAt(i - 1) == '\n')) return next.match(m, i);
            return false;
        }
    }

    static final class Dollar extends Node {
        final boolean multiline;

        Dollar(boolean ml) { multiline = ml; }

        boolean match(Matcher m, int i) {
            int to = m.to;
            if (multiline) {
                if (i == to || m.text.charAt(i) == '\n' || m.text.charAt(i) == '\r') return next.match(m, i);
                return false;
            }
            if (i == to) return next.match(m, i);
            if (i == to - 1 && m.text.charAt(i) == '\n') return next.match(m, i);
            if (i == to - 2 && m.text.charAt(i) == '\r' && m.text.charAt(i + 1) == '\n') return next.match(m, i);
            return false;
        }
    }

    static final class End extends Node {
        final boolean allowNewline;

        End(boolean nl) { allowNewline = nl; }

        boolean match(Matcher m, int i) {
            if (i == m.to || (allowNewline && i == m.to - 1 && m.text.charAt(i) == '\n')) return next.match(m, i);
            return false;
        }
    }

    static final class Bound extends Node {
        final boolean negate;

        Bound(boolean negate) { this.negate = negate; }

        boolean match(Matcher m, int i) {
            boolean a = i > m.from && CharPred.WORD.is(m.text.charAt(i - 1));
            boolean b = i < m.to && CharPred.WORD.is(m.text.charAt(i));
            return ((a != b) != negate) && next.match(m, i);
        }
    }

    static final class Look extends Node {
        final Node body;
        final boolean negate;

        Look(Node body, boolean negate) {
            this.body = body;
            this.negate = negate;
        }

        boolean match(Matcher m, int i) {
            boolean saveAnchor = m.anchorEnd;
            int saveLast = m.last;
            m.anchorEnd = false;
            boolean ok = body.match(m, i);
            m.anchorEnd = saveAnchor;
            m.last = saveLast;
            return ok != negate && next.match(m, i);
        }
    }

    static final class LookBehindEnd extends Node {
        int target;

        boolean match(Matcher m, int i) { return i == target; }
    }

    static final class LookBehind extends Node {
        final Node body;
        final LookBehindEnd end;
        final boolean negate;

        LookBehind(Node body, LookBehindEnd end, boolean negate) {
            this.body = body;
            this.end = end;
            this.negate = negate;
        }

        boolean match(Matcher m, int i) {
            int saveTarget = end.target;
            end.target = i;
            boolean found = false;
            for (int s = i; s >= m.from && !found; s--) found = body.match(m, s);
            end.target = saveTarget;
            return found != negate && next.match(m, i);
        }
    }

    /* ------------------------------------------------------------ character predicates */
    static abstract class CharPred {
        abstract boolean is(int c);

        static boolean eqIgnoreCase(char a, char b) {
            return Character.toUpperCase(a) == Character.toUpperCase(b) || Character.toLowerCase(a) == Character.toLowerCase(b);
        }

        static final CharPred ANY = new CharPred() {
            boolean is(int c) { return true; }
        };

        static final CharPred DOT = new CharPred() {
            boolean is(int c) { return c != '\n' && c != '\r' && c != '\u0085' && c != '\u2028' && c != '\u2029'; }
        };

        static final CharPred DIGIT = new CharPred() {
            boolean is(int c) { return c >= '0' && c <= '9'; }
        };

        static final CharPred WORD = new CharPred() {
            boolean is(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
        };

        static final CharPred SPACE = new CharPred() {
            boolean is(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\u000B' || c == '\f' || c == '\r'; }
        };

        static CharPred literal(char c, boolean ci) { return ci ? new Range(c, c, true) : new Lit(c); }

        static final class Lit extends CharPred {
            final char c;

            Lit(char c) { this.c = c; }

            boolean is(int x) { return x == c; }
        }

        static final class Range extends CharPred {
            final int lo, hi;
            final boolean ci;

            Range(int lo, int hi, boolean ci) {
                this.lo = lo;
                this.hi = hi;
                this.ci = ci;
            }

            boolean is(int c) {
                if (c >= lo && c <= hi) return true;
                if (!ci) return false;
                int u = Character.toUpperCase(c), l = Character.toLowerCase(c);
                return (u >= lo && u <= hi) || (l >= lo && l <= hi);
            }
        }

        static final class Set extends CharPred {
            final String chars;

            Set(String chars) { this.chars = chars; }

            boolean is(int c) { return chars.indexOf(c) >= 0; }
        }

        static final class Union extends CharPred {
            final ArrayList<CharPred> parts = new ArrayList<CharPred>();

            void add(CharPred p) { parts.add(p); }

            boolean is(int c) {
                for (int i = 0; i < parts.size(); i++) if (parts.get(i).is(c)) return true;
                return false;
            }
        }

        static CharPred not(final CharPred p) {
            return new CharPred() {
                boolean is(int c) { return !p.is(c); }
            };
        }

        static CharPred and(final CharPred a, final CharPred b) {
            return new CharPred() {
                boolean is(int c) { return a.is(c) && b.is(c); }
            };
        }

        static CharPred property(String name) {
            if (name.startsWith("Is")) name = name.substring(2);
            if (name.equals("Lower") || name.equals("Ll") || name.equals("javaLowerCase") || name.equals("Lowercase"))
                return new CharPred() { boolean is(int c) { return Character.isLowerCase(c); } };
            if (name.equals("Upper") || name.equals("Lu") || name.equals("javaUpperCase") || name.equals("Uppercase"))
                return new CharPred() { boolean is(int c) { return Character.isUpperCase(c); } };
            if (name.equals("ASCII")) return new Range(0, 127, false);
            if (name.equals("Alpha")) return new CharPred() { boolean is(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); } };
            if (name.equals("Digit")) return DIGIT;
            if (name.equals("Alnum")) return new CharPred() { boolean is(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); } };
            if (name.equals("Punct") || name.equals("P"))
                return new Set("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
            if (name.equals("Graph")) return new Range(0x21, 0x7E, false);
            if (name.equals("Print")) return new Range(0x20, 0x7E, false);
            if (name.equals("Blank")) return new Set(" \t");
            if (name.equals("Cntrl")) return new CharPred() { boolean is(int c) { return c < 0x20 || c == 0x7F; } };
            if (name.equals("XDigit")) return new CharPred() { boolean is(int c) { return Character.digit(c, 16) >= 0 && c < 128; } };
            if (name.equals("Space") || name.equals("javaWhitespace") || name.equals("White_Space") || name.equals("Z") || name.equals("Zs"))
                return new CharPred() { boolean is(int c) { return Character.isWhitespace(c); } };
            if (name.equals("L") || name.equals("Alphabetic") || name.equals("Letter") || name.equals("javaLetter"))
                return new CharPred() { boolean is(int c) { return Character.isLetter(c); } };
            if (name.equals("N") || name.equals("Nd") || name.equals("javaDigit"))
                return new CharPred() { boolean is(int c) { return Character.isDigit(c); } };
            if (name.equals("javaLetterOrDigit")) return new CharPred() { boolean is(int c) { return Character.isLetterOrDigit(c); } };
            if (name.equals("Thai")) return new Range(0x0E00, 0x0E7F, false);
            if (name.equals("Han")) return new Range(0x4E00, 0x9FFF, false);
            return null;
        }
    }
}

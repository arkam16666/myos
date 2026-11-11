package java.util;

public class StringTokenizer implements Enumeration<Object> {
    private int currentPosition, maxPosition;
    private String str, delimiters;
    private boolean retDelims;

    public StringTokenizer(String str, String delim, boolean returnDelims) {
        this.str = str;
        this.maxPosition = str.length();
        this.delimiters = delim;
        this.retDelims = returnDelims;
    }

    public StringTokenizer(String str, String delim) { this(str, delim, false); }

    public StringTokenizer(String str) { this(str, " \t\n\r\f", false); }

    private int skipDelimiters(int startPos) {
        int position = startPos;
        while (!retDelims && position < maxPosition && delimiters.indexOf(str.charAt(position)) >= 0) position++;
        return position;
    }

    private int scanToken(int startPos) {
        int position = startPos;
        while (position < maxPosition && delimiters.indexOf(str.charAt(position)) < 0) position++;
        if (retDelims && startPos == position && position < maxPosition) position++;
        return position;
    }

    public boolean hasMoreTokens() { return skipDelimiters(currentPosition) < maxPosition; }

    public String nextToken() {
        currentPosition = skipDelimiters(currentPosition);
        if (currentPosition >= maxPosition) throw new NoSuchElementException();
        int start = currentPosition;
        currentPosition = scanToken(currentPosition);
        return str.substring(start, currentPosition);
    }

    public String nextToken(String delim) {
        delimiters = delim;
        return nextToken();
    }

    public boolean hasMoreElements() { return hasMoreTokens(); }

    public Object nextElement() { return nextToken(); }

    public int countTokens() {
        int count = 0, currpos = currentPosition;
        while (currpos < maxPosition) {
            currpos = skipDelimiters(currpos);
            if (currpos >= maxPosition) break;
            currpos = scanToken(currpos);
            count++;
        }
        return count;
    }
}

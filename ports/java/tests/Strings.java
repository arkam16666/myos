import java.util.Arrays;

public class Strings {
    static String classify(String s) {
        switch (s) {
            case "apple":
            case "banana":
                return "fruit";
            case "carrot":
                return "vegetable";
            default:
                return "unknown";
        }
    }

    public static void main(String[] args) {
        String s = "Hello, World";
        System.out.println(s.length() + " " + s.charAt(4) + " " + s.indexOf('o') + " " + s.lastIndexOf("o") + " " + s.indexOf("xyz"));
        System.out.println(s.substring(7) + "|" + s.substring(0, 5) + "|" + s.toUpperCase() + "|" + s.toLowerCase());
        System.out.println(s.replace('l', 'L') + "|" + s.replace("World", "MyOS") + "|" + s.contains("lo,") + "|" + s.startsWith("Hell"));
        System.out.println("  trim me  ".trim() + "|" + "  strip  ".strip() + "|" + "".isEmpty() + "|" + "  ".isBlank());
        System.out.println("a,b,,c,".split(",").length + " " + Arrays.toString("a,b,,c,".split(",")) + " " + Arrays.toString("a1b22c333".split("\\d+")));
        System.out.println(Arrays.toString("one  two three".split("\\s+")) + " " + Arrays.toString("x.y.z".split("\\.")) + " " + Arrays.toString("k=v".split("=", 2)));
        System.out.println(String.join("-", "a", "b", "c") + " " + String.join("/", Arrays.asList("x", "y")));
        System.out.println("ab".repeat(3) + " " + "abc".compareTo("abd") + " " + "b".compareTo("a") + " " + "Abc".equalsIgnoreCase("aBC"));
        System.out.println("hello".hashCode() + " " + "".hashCode() + " " + "The quick brown fox".hashCode());
        System.out.println(classify("apple") + " " + classify("carrot") + " " + classify("rock"));
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 5; i++) sb.append(i).append(',');
        sb.setLength(sb.length() - 1);
        sb.insert(0, "[").append("]").reverse();
        System.out.println(sb + " " + sb.length() + " " + sb.indexOf(","));
        sb = new StringBuilder("abcdef");
        sb.deleteCharAt(2).replace(0, 1, "XY").setCharAt(1, 'z');
        System.out.println(sb.toString() + " " + sb.charAt(0));
        char[] cs = "dcba".toCharArray();
        Arrays.sort(cs);
        System.out.println(new String(cs) + " " + String.valueOf(cs, 1, 2) + " " + String.valueOf(3.5) + String.valueOf(true));
        System.out.println("x=" + 1 + 2 + " " + (1 + 2) + "y" + 'c' + 1.5f + null + 'z');
        String t = null;
        System.out.println("null concat: " + t);
        System.out.println("tab\tquote\"backslash\\ unicode:éก");
        System.out.println("matches: " + "abc123".matches("[a-z]+\\d+") + " " + "2024-01-15".matches("\\d{4}-\\d{2}-\\d{2}") + " " + "abc".matches("a.c|x"));
        System.out.println("a1b2c3".replaceAll("[0-9]", "#") + " " + "John Smith".replaceAll("(\\w+) (\\w+)", "$2, $1") + " " + "aaa".replaceFirst("a", "b"));
        System.out.println("Mississippi".chars().filter(ch -> ch == 's').count() + " " + "hello".indexOf("l", 3));
        int count = 0;
        for (char ch : "programming".toCharArray()) if ("aeiou".indexOf(ch) >= 0) count++;
        System.out.println("vowels " + count);
        String pal = "racecar";
        System.out.println(new StringBuilder(pal).reverse().toString().equals(pal) ? "palindrome" : "not");
        System.out.println(String.valueOf((Object) null) + " " + "a".concat("b") + " " + "abc".contentEquals("abc"));
        System.out.println("Line1\nLine2".lines().count() + " " + "%d items".formatted(3) + " " + "café".length());
        System.out.println("A".codePointAt(0) + " " + Character.isLetter('x') + Character.isDigit('5') + Character.isWhitespace(' ') + Character.toUpperCase('q'));
        String interned = new String("abc").intern();
        System.out.println((interned == "abc") + " " + (new String("abc") == "abc") + " " + "abc".equals(new String("abc")));
    }
}

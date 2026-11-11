package java.lang;

/** Java 25's java.lang.IO: console input and output for simple programs. */
public final class IO {
    private IO() {}

    public static void println(Object obj) { System.out.println(obj); }

    public static void println() { System.out.println(); }

    public static void print(Object obj) {
        System.out.print(obj);
        System.out.flush();
    }

    public static String readln(String prompt) {
        print(prompt);
        return readln();
    }

    public static String readln() {
        return java.io.Console.readLine0();
    }
}

package java.io;

/** System.console() is null on MyOS; this only holds the shared console reader. */
public final class Console {
    private static BufferedReader reader;

    private Console() {}

    public static String readLine0() {
        try {
            if (reader == null) reader = new BufferedReader(new InputStreamReader(System.in));
            return reader.readLine();
        } catch (IOException e) {
            return null;
        }
    }
}

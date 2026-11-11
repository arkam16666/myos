package java.nio.file;

import java.io.*;
import java.util.ArrayList;
import java.util.List;

/** The common text-file helpers of java.nio.file.Files, on MyFS. */
public final class Files {
    private Files() {}

    public static byte[] readAllBytes(Path path) throws IOException {
        FileInputStream in = new FileInputStream(path.toString());
        try {
            return in.readAllBytes();
        } finally {
            in.close();
        }
    }

    public static String readString(Path path) throws IOException { return new String(readAllBytes(path)); }

    public static String readString(Path path, java.nio.charset.Charset cs) throws IOException { return readString(path); }

    public static List<String> readAllLines(Path path) throws IOException {
        BufferedReader r = new BufferedReader(new FileReader(path.toString()));
        List<String> lines = new ArrayList<String>();
        try {
            String l;
            while ((l = r.readLine()) != null) lines.add(l);
        } finally {
            r.close();
        }
        return lines;
    }

    public static List<String> readAllLines(Path path, java.nio.charset.Charset cs) throws IOException { return readAllLines(path); }

    public static java.util.stream.Stream<String> lines(Path path) throws IOException { return readAllLines(path).stream(); }

    public static BufferedReader newBufferedReader(Path path) throws IOException { return new BufferedReader(new FileReader(path.toString())); }

    public static BufferedWriter newBufferedWriter(Path path, OpenOption... options) throws IOException {
        return new BufferedWriter(new FileWriter(path.toString(), append(options)));
    }

    private static boolean append(OpenOption[] options) {
        for (OpenOption o : options) if (o == StandardOpenOption.APPEND) return true;
        return false;
    }

    public static Path write(Path path, byte[] bytes, OpenOption... options) throws IOException {
        FileOutputStream out = new FileOutputStream(path.toString(), append(options));
        try {
            out.write(bytes);
        } finally {
            out.close();
        }
        return path;
    }

    public static Path writeString(Path path, CharSequence csq, OpenOption... options) throws IOException {
        return write(path, csq.toString().getBytes(), options);
    }

    public static Path writeString(Path path, CharSequence csq, java.nio.charset.Charset cs, OpenOption... options) throws IOException {
        return writeString(path, csq, options);
    }

    public static Path write(Path path, Iterable<? extends CharSequence> lines, OpenOption... options) throws IOException {
        StringBuilder sb = new StringBuilder();
        for (CharSequence l : lines) sb.append(l).append('\n');
        return writeString(path, sb, options);
    }

    public static boolean exists(Path path, LinkOption... options) { return path.toFile().exists(); }

    public static boolean notExists(Path path, LinkOption... options) { return !exists(path); }

    public static boolean isDirectory(Path path, LinkOption... options) { return path.toFile().isDirectory(); }

    public static boolean isRegularFile(Path path, LinkOption... options) { return path.toFile().isFile(); }

    public static long size(Path path) throws IOException { return path.toFile().length(); }

    public static void delete(Path path) throws IOException {
        if (!path.toFile().delete()) throw new NoSuchFileException(path.toString());
    }

    public static boolean deleteIfExists(Path path) throws IOException { return path.toFile().delete(); }

    public static Path createDirectory(Path dir, Object... attrs) throws IOException {
        if (!dir.toFile().mkdir()) throw new IOException("can't create " + dir);
        return dir;
    }

    public static Path createDirectories(Path dir, Object... attrs) throws IOException {
        dir.toFile().mkdirs();
        return dir;
    }

    public static Path createFile(Path path, Object... attrs) throws IOException {
        if (!path.toFile().createNewFile()) throw new IOException("file exists: " + path);
        return path;
    }
}

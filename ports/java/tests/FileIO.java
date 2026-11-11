import java.io.*;
import java.nio.file.*;
import java.util.*;

/* text files on the disk (MyFS on MyOS) */
public class FileIO {
    public static void main(String[] args) throws IOException {
        File f = new File("notes.txt");
        try (PrintWriter out = new PrintWriter(new FileWriter(f))) {
            out.println("first line");
            out.printf("%d squared is %d%n", 12, 144);
            out.println("ภาษาไทย ok");
        }
        System.out.println(f.exists() + " " + f.getName() + " " + (f.length() >= 54));
        try (FileWriter app = new FileWriter("notes.txt", true)) {
            app.write("appended\n");
        }
        try (BufferedReader in = new BufferedReader(new FileReader("notes.txt"))) {
            String line;
            int n = 0;
            while ((line = in.readLine()) != null) System.out.println(++n + ": " + line);
        }
        Scanner sc = new Scanner(new File("notes.txt"));
        int words = 0;
        while (sc.hasNext()) {
            sc.next();
            words++;
        }
        sc.close();
        System.out.println("words " + words);
        Path p = Paths.get("data.csv");
        Files.write(p, Arrays.asList("name,score", "ann,90", "bob,85"));
        List<String> rows = Files.readAllLines(p);
        int total = 0;
        for (String r : rows.subList(1, rows.size())) total += Integer.parseInt(r.split(",")[1]);
        System.out.println(rows.size() + " rows, total " + total + ", " + Files.exists(p));
        Files.writeString(Paths.get("one.txt"), "just one line");
        System.out.println(Files.readString(Paths.get("one.txt")).toUpperCase());
        try (DataLikeCopy c = new DataLikeCopy()) {
            c.copy("one.txt", "two.txt");
        }
        System.out.println(new File("two.txt").length());
        try {
            new FileReader("missing.txt");
        } catch (FileNotFoundException e) {
            System.out.println("FileNotFoundException caught");
        }
        System.out.println(new File("notes.txt").delete() + " " + new File("notes.txt").exists() + " " + new File("data.csv").delete()
                + new File("one.txt").delete() + new File("two.txt").delete());
    }

    static class DataLikeCopy implements Closeable {
        void copy(String from, String to) throws IOException {
            try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
                byte[] buf = new byte[4];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
        }

        public void close() {}
    }
}

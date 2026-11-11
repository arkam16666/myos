package java.io;

public class FileReader extends InputStreamReader {
    public FileReader(String fileName) throws FileNotFoundException { super(new FileInputStream(fileName)); }

    public FileReader(File file) throws FileNotFoundException { super(new FileInputStream(file)); }

    public FileReader(String fileName, java.nio.charset.Charset cs) throws IOException { this(fileName); }

    public FileReader(File file, java.nio.charset.Charset cs) throws IOException { this(file); }
}

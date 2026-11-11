package java.io;

public class FileInputStream extends InputStream {
    private int fd;

    public FileInputStream(String name) throws FileNotFoundException {
        if (name == null) throw new NullPointerException();
        fd = open0(name);
    }

    public FileInputStream(File file) throws FileNotFoundException { this(file.getPath()); }

    public FileInputStream(FileDescriptor fdObj) { fd = fdObj.fd; }

    private static native int open0(String name) throws FileNotFoundException;

    private static native int read0(int fd) throws IOException;

    private static native int readBytes0(int fd, byte[] b, int off, int len) throws IOException;

    private static native int available0(int fd) throws IOException;

    private static native void close0(int fd);

    public int read() throws IOException { return read0(fd); }

    public int read(byte[] b, int off, int len) throws IOException { return readBytes0(fd, b, off, len); }

    public int available() throws IOException { return available0(fd); }

    public void close() throws IOException {
        if (fd > 2) close0(fd);
        fd = -1;
    }
}

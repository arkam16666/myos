package java.io;

public class FileOutputStream extends OutputStream {
    int fd;

    public FileOutputStream(String name) throws FileNotFoundException { this(name, false); }

    public FileOutputStream(String name, boolean append) throws FileNotFoundException {
        if (name == null) throw new NullPointerException();
        fd = open0(name, append);
    }

    public FileOutputStream(File file) throws FileNotFoundException { this(file.getPath(), false); }

    public FileOutputStream(File file, boolean append) throws FileNotFoundException { this(file.getPath(), append); }

    public FileOutputStream(FileDescriptor fdObj) { fd = fdObj.fd; }

    private static native int open0(String name, boolean append) throws FileNotFoundException;

    static native void writeBytes0(int fd, byte[] b, int off, int len) throws IOException;

    static native void writeString0(int fd, String s) throws IOException;

    static native void flush0(int fd);

    private static native void close0(int fd);

    public void write(int b) throws IOException { writeBytes0(fd, new byte[] { (byte) b }, 0, 1); }

    public void write(byte[] b, int off, int len) throws IOException { writeBytes0(fd, b, off, len); }

    public void flush() throws IOException { flush0(fd); }

    public void close() throws IOException {
        if (fd > 2) close0(fd);
        else if (fd >= 0) flush0(fd);
        fd = -1;
    }
}

package java.io;

public final class FileDescriptor {
    final int fd;
    public static final FileDescriptor in = new FileDescriptor(0);
    public static final FileDescriptor out = new FileDescriptor(1);
    public static final FileDescriptor err = new FileDescriptor(2);

    private FileDescriptor(int fd) { this.fd = fd; }

    public FileDescriptor() { fd = -1; }

    public boolean valid() { return fd >= 0; }

    public void sync() {}
}

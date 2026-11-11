package java.lang;

public final class StackTraceElement implements java.io.Serializable {
    private String declaringClass;
    private String methodName;
    private String fileName;
    private int lineNumber;

    public StackTraceElement(String declaringClass, String methodName, String fileName, int lineNumber) {
        this.declaringClass = declaringClass;
        this.methodName = methodName;
        this.fileName = fileName;
        this.lineNumber = lineNumber;
    }

    public String getClassName() { return declaringClass; }

    public String getMethodName() { return methodName; }

    public String getFileName() { return fileName; }

    public int getLineNumber() { return lineNumber; }

    public boolean isNativeMethod() { return lineNumber == -2; }

    public String toString() {
        String where;
        if (lineNumber == -2) where = "Native Method";
        else if (fileName == null) where = "Unknown Source";
        else if (lineNumber >= 0) where = fileName + ":" + lineNumber;
        else where = fileName;
        return declaringClass + "." + methodName + "(" + where + ")";
    }

    public boolean equals(Object o) {
        if (!(o instanceof StackTraceElement)) return false;
        StackTraceElement e = (StackTraceElement) o;
        return e.lineNumber == lineNumber && java.util.Objects.equals(declaringClass, e.declaringClass)
                && java.util.Objects.equals(methodName, e.methodName) && java.util.Objects.equals(fileName, e.fileName);
    }

    public int hashCode() { return 31 * declaringClass.hashCode() + methodName.hashCode() * 7 + lineNumber; }
}

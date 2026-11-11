package java.text;

public abstract class Format implements java.io.Serializable, Cloneable {
    protected Format() {}

    public final String format(Object obj) { return format(obj, new StringBuffer(), null).toString(); }

    public abstract StringBuffer format(Object obj, StringBuffer toAppendTo, FieldPosition pos);

    public Object clone() {
        try {
            return super.clone();
        } catch (CloneNotSupportedException e) {
            throw new InternalError(e);
        }
    }
}

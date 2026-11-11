package java.math;

/** Decimal numbers: an unscaled BigInteger times 10^-scale. */
public class BigDecimal extends Number implements Comparable<BigDecimal> {
    private final BigInteger intVal;
    private final int scale;

    public static final BigDecimal ZERO = new BigDecimal(BigInteger.ZERO, 0);
    public static final BigDecimal ONE = new BigDecimal(BigInteger.ONE, 0);
    public static final BigDecimal TWO = new BigDecimal(BigInteger.TWO, 0);
    public static final BigDecimal TEN = new BigDecimal(BigInteger.TEN, 0);

    public static final int ROUND_UP = 0, ROUND_DOWN = 1, ROUND_CEILING = 2, ROUND_FLOOR = 3, ROUND_HALF_UP = 4,
            ROUND_HALF_DOWN = 5, ROUND_HALF_EVEN = 6, ROUND_UNNECESSARY = 7;

    public BigDecimal(BigInteger unscaledVal, int scale) {
        this.intVal = unscaledVal;
        this.scale = scale;
    }

    public BigDecimal(BigInteger val) { this(val, 0); }

    public BigDecimal(int val) { this(BigInteger.valueOf(val), 0); }

    public BigDecimal(long val) { this(BigInteger.valueOf(val), 0); }

    public BigDecimal(String val) {
        String s = val.trim();
        int e = Math.max(s.indexOf('e'), s.indexOf('E'));
        int exp = 0;
        if (e >= 0) {
            String es = s.substring(e + 1);
            if (es.startsWith("+")) es = es.substring(1);
            exp = Integer.parseInt(es);
            s = s.substring(0, e);
        }
        int dot = s.indexOf('.');
        int sc = 0;
        if (dot >= 0) {
            sc = s.length() - dot - 1;
            s = s.substring(0, dot) + s.substring(dot + 1);
        }
        if (s.isEmpty() || s.equals("-") || s.equals("+")) throw new NumberFormatException("Character array is missing \"exponent\" mark 'e' or 'E'.");
        try {
            intVal = new BigInteger(s);
        } catch (NumberFormatException ex) {
            throw new NumberFormatException("Character " + val + " is neither a decimal digit number, decimal point, nor \"e\" notation exponential mark.");
        }
        scale = sc - exp;
    }

    public BigDecimal(char[] in) { this(new String(in)); }

    public BigDecimal(double val) {
        if (Double.isNaN(val) || Double.isInfinite(val)) throw new NumberFormatException("Infinite or NaN");
        long bits = Double.doubleToLongBits(val);
        int sign = (bits >> 63) == 0 ? 1 : -1;
        int exponent = (int) ((bits >> 52) & 0x7ffL);
        long m = exponent == 0 ? (bits & ((1L << 52) - 1)) << 1 : (bits & ((1L << 52) - 1)) | (1L << 52);
        exponent -= 1075;
        if (m == 0) {
            intVal = BigInteger.ZERO;
            scale = 0;
            return;
        }
        while ((m & 1) == 0 && exponent < 0) {
            m >>= 1;
            exponent++;
        }
        BigInteger mm = BigInteger.valueOf(sign * m);
        if (exponent >= 0) {
            intVal = mm.shiftLeft(exponent);
            scale = 0;
        } else {
            intVal = mm.multiply(BigInteger.valueOf(5).pow(-exponent));
            scale = -exponent;
        }
    }

    public BigDecimal(String val, MathContext mc) { this(new BigDecimal(val).round(mc)); }

    private BigDecimal(BigDecimal v) { this(v.intVal, v.scale); }

    public static BigDecimal valueOf(long val) { return new BigDecimal(BigInteger.valueOf(val), 0); }

    public static BigDecimal valueOf(long unscaledVal, int scale) { return new BigDecimal(BigInteger.valueOf(unscaledVal), scale); }

    public static BigDecimal valueOf(double val) { return new BigDecimal(Double.toString(val)); }

    private static BigInteger tenPow(int n) { return BigInteger.TEN.pow(n); }

    private BigInteger scaledTo(int newScale) {     /* newScale >= scale */
        return newScale == scale ? intVal : intVal.multiply(tenPow(newScale - scale));
    }

    public BigDecimal add(BigDecimal augend) {
        int s = Math.max(scale, augend.scale);
        return new BigDecimal(scaledTo(s).add(augend.scaledTo(s)), s);
    }

    public BigDecimal add(BigDecimal augend, MathContext mc) { return add(augend).round(mc); }

    public BigDecimal subtract(BigDecimal subtrahend) { return add(subtrahend.negate()); }

    public BigDecimal subtract(BigDecimal subtrahend, MathContext mc) { return subtract(subtrahend).round(mc); }

    public BigDecimal multiply(BigDecimal multiplicand) {
        return new BigDecimal(intVal.multiply(multiplicand.intVal), scale + multiplicand.scale);
    }

    public BigDecimal multiply(BigDecimal multiplicand, MathContext mc) { return multiply(multiplicand).round(mc); }

    /* round the quotient n / d (both integers) to an integer */
    private static BigInteger roundDiv(BigInteger n, BigInteger d, RoundingMode mode) {
        BigInteger[] qr = n.divideAndRemainder(d);
        BigInteger q = qr[0], r = qr[1];
        if (r.signum() == 0) return q;
        int sign = n.signum() * d.signum();
        int half = r.abs().shiftLeft(1).compareTo(d.abs());     /* <0 below half, 0 half, >0 above */
        boolean up;
        switch (mode) {
            case UP: up = true; break;
            case DOWN: up = false; break;
            case CEILING: up = sign > 0; break;
            case FLOOR: up = sign < 0; break;
            case HALF_UP: up = half >= 0; break;
            case HALF_DOWN: up = half > 0; break;
            case HALF_EVEN: up = half > 0 || (half == 0 && q.testBit(0)); break;
            default: throw new ArithmeticException("Rounding necessary");
        }
        return up ? q.add(BigInteger.valueOf(sign)) : q;
    }

    public BigDecimal divide(BigDecimal divisor, int scale, RoundingMode roundingMode) {
        if (divisor.signum() == 0) throw new ArithmeticException(signum() == 0 ? "Division undefined" : "Division by zero");
        /* this / divisor = (intVal * 10^(scale + divisor.scale - this.scale)) / divisor.intVal */
        int shift = scale + divisor.scale - this.scale;
        BigInteger n = intVal, d = divisor.intVal;
        if (shift >= 0) n = n.multiply(tenPow(shift));
        else d = d.multiply(tenPow(-shift));
        return new BigDecimal(roundDiv(n, d, roundingMode), scale);
    }

    public BigDecimal divide(BigDecimal divisor, int scale, int roundingMode) {
        return divide(divisor, scale, RoundingMode.valueOf(roundingMode));
    }

    public BigDecimal divide(BigDecimal divisor, RoundingMode roundingMode) { return divide(divisor, scale, roundingMode); }

    public BigDecimal divide(BigDecimal divisor, int roundingMode) { return divide(divisor, scale, RoundingMode.valueOf(roundingMode)); }

    public BigDecimal divide(BigDecimal divisor) {
        if (divisor.signum() == 0) throw new ArithmeticException(signum() == 0 ? "Division undefined" : "Division by zero");
        int preferred = scale - divisor.scale;
        if (signum() == 0) return new BigDecimal(BigInteger.ZERO, Math.max(preferred, 0));
        /* exact only if the reduced divisor has no prime factors but 2 and 5 */
        BigInteger d = divisor.intVal.abs();
        BigInteger g = d.gcd(intVal.abs());
        BigInteger rd = d.divide(g);
        int twos = 0, fives = 0;
        while (!rd.testBit(0)) {
            rd = rd.shiftRight(1);
            twos++;
        }
        BigInteger five = BigInteger.valueOf(5);
        while (rd.mod(five).signum() == 0) {
            rd = rd.divide(five);
            fives++;
        }
        if (!rd.equals(BigInteger.ONE))
            throw new ArithmeticException("Non-terminating decimal expansion; no exact representable decimal result.");
        int extra = Math.max(twos, fives);
        BigDecimal q = divide(divisor, Math.max(preferred, scale + extra), RoundingMode.UNNECESSARY);
        /* strip back down toward the preferred scale */
        while (q.scale > preferred) {
            BigInteger[] qr = q.intVal.divideAndRemainder(BigInteger.TEN);
            if (qr[1].signum() != 0) break;
            q = new BigDecimal(qr[0], q.scale - 1);
        }
        return q;
    }

    public BigDecimal divide(BigDecimal divisor, MathContext mc) {
        if (mc.getPrecision() == 0) return divide(divisor);
        /* enough digits, then round to the precision */
        int digits = mc.getPrecision() + 3;
        int sc = digits - (precision() - scale) + (divisor.precision() - divisor.scale) ;
        BigDecimal q = divide(divisor, Math.max(sc, scale), mc.getRoundingMode());
        return q.round(mc).stripZerosTo(scale - divisor.scale);
    }

    private BigDecimal stripZerosTo(int preferred) {
        BigDecimal q = this;
        while (q.scale > preferred && q.intVal.signum() != 0) {
            BigInteger[] qr = q.intVal.divideAndRemainder(BigInteger.TEN);
            if (qr[1].signum() != 0) break;
            q = new BigDecimal(qr[0], q.scale - 1);
        }
        return q;
    }

    public BigDecimal divideToIntegralValue(BigDecimal divisor) {
        return divide(divisor, 0, RoundingMode.DOWN).setScale(Math.max(0, scale - divisor.scale));
    }

    public BigDecimal remainder(BigDecimal divisor) {
        BigDecimal q = divide(divisor, 0, RoundingMode.DOWN);
        return subtract(q.multiply(divisor));
    }

    public BigDecimal[] divideAndRemainder(BigDecimal divisor) {
        BigDecimal q = divideToIntegralValue(divisor);
        return new BigDecimal[] { q, subtract(q.multiply(divisor)) };
    }

    public BigDecimal pow(int n) {
        if (n < 0 || n > 999999999) throw new ArithmeticException("Invalid operation");
        return new BigDecimal(intVal.pow(n), scale * n);
    }

    public BigDecimal sqrt(MathContext mc) {
        if (signum() < 0) throw new ArithmeticException("Attempted square root of negative BigDecimal");
        int prec = mc.getPrecision() == 0 ? precision() / 2 + 1 : mc.getPrecision();
        int sc = prec + 2;
        BigInteger n = intVal.multiply(tenPow(2 * sc + (scale % 2 != 0 ? 1 : 0)));
        int resultScale = sc + (scale + (scale % 2 != 0 ? 1 : 0)) / 2;
        return new BigDecimal(n.sqrt(), resultScale).round(new MathContext(prec, mc.getRoundingMode())).stripTrailingZeros();
    }

    public BigDecimal negate() { return new BigDecimal(intVal.negate(), scale); }

    public BigDecimal negate(MathContext mc) { return negate().round(mc); }

    public BigDecimal abs() { return signum() < 0 ? negate() : this; }

    public BigDecimal abs(MathContext mc) { return abs().round(mc); }

    public BigDecimal plus() { return this; }

    public BigDecimal plus(MathContext mc) { return round(mc); }

    public int signum() { return intVal.signum(); }

    public int scale() { return scale; }

    public int precision() {
        if (intVal.signum() == 0) return 1;
        return intVal.abs().toString().length();
    }

    public BigInteger unscaledValue() { return intVal; }

    public BigDecimal round(MathContext mc) {
        int p = mc.getPrecision();
        if (p == 0) return this;
        int drop = precision() - p;
        if (drop <= 0) return this;
        BigInteger r = roundDiv(intVal, tenPow(drop), mc.getRoundingMode());
        BigDecimal res = new BigDecimal(r, scale - drop);
        if (res.precision() > p) res = res.round(mc);      /* 9.99 -> 10.0 */
        return res;
    }

    public BigDecimal setScale(int newScale, RoundingMode roundingMode) {
        if (newScale == scale) return this;
        if (newScale > scale) return new BigDecimal(scaledTo(newScale), newScale);
        return new BigDecimal(roundDiv(intVal, tenPow(scale - newScale), roundingMode), newScale);
    }

    public BigDecimal setScale(int newScale, int roundingMode) { return setScale(newScale, RoundingMode.valueOf(roundingMode)); }

    public BigDecimal setScale(int newScale) { return setScale(newScale, RoundingMode.UNNECESSARY); }

    public BigDecimal movePointLeft(int n) {
        BigDecimal r = new BigDecimal(intVal, scale + n);
        return r.scale < 0 ? r.setScale(0) : r;
    }

    public BigDecimal movePointRight(int n) {
        BigDecimal r = new BigDecimal(intVal, scale - n);
        return r.scale < 0 ? r.setScale(0) : r;
    }

    public BigDecimal scaleByPowerOfTen(int n) { return new BigDecimal(intVal, scale - n); }

    public BigDecimal stripTrailingZeros() {
        if (intVal.signum() == 0) return ZERO;
        return stripZerosTo(Integer.MIN_VALUE);
    }

    public int compareTo(BigDecimal val) {
        int s = Math.max(scale, val.scale);
        return scaledTo(s).compareTo(val.scaledTo(s));
    }

    public boolean equals(Object x) {
        if (!(x instanceof BigDecimal)) return false;
        BigDecimal o = (BigDecimal) x;
        return scale == o.scale && intVal.equals(o.intVal);
    }

    public int hashCode() { return 31 * intVal.hashCode() + scale; }

    public BigDecimal min(BigDecimal val) { return compareTo(val) <= 0 ? this : val; }

    public BigDecimal max(BigDecimal val) { return compareTo(val) >= 0 ? this : val; }

    public String toString() {
        String digits = intVal.abs().toString();
        String sign = intVal.signum() < 0 ? "-" : "";
        if (scale == 0) return sign + digits;
        int adjusted = -scale + (digits.length() - 1);
        if (scale > 0 && adjusted >= -6) return sign + plain(digits, scale);
        StringBuilder sb = new StringBuilder(sign).append(digits.charAt(0));
        if (digits.length() > 1) sb.append('.').append(digits, 1, digits.length());
        sb.append('E');
        if (adjusted >= 0) sb.append('+');
        return sb.append(adjusted).toString();
    }

    private static String plain(String digits, int scale) {
        if (scale <= 0) {
            StringBuilder sb = new StringBuilder(digits);
            for (int i = 0; i < -scale; i++) sb.append('0');
            return sb.toString();
        }
        if (digits.length() > scale) return digits.substring(0, digits.length() - scale) + "." + digits.substring(digits.length() - scale);
        StringBuilder sb = new StringBuilder("0.");
        for (int i = digits.length(); i < scale; i++) sb.append('0');
        return sb.append(digits).toString();
    }

    public String toPlainString() {
        String digits = intVal.abs().toString();
        return (intVal.signum() < 0 ? "-" : "") + plain(digits, scale);
    }

    public String toEngineeringString() { return toString(); }

    public BigInteger toBigInteger() { return setScale(0, RoundingMode.DOWN).intVal; }

    public BigInteger toBigIntegerExact() { return setScale(0, RoundingMode.UNNECESSARY).intVal; }

    public double doubleValue() { return Double.parseDouble(toString()); }

    public float floatValue() { return (float) doubleValue(); }

    public long longValue() { return toBigInteger().longValue(); }

    public int intValue() { return toBigInteger().intValue(); }

    public int intValueExact() { return toBigIntegerExact().intValueExact(); }

    public long longValueExact() { return toBigIntegerExact().longValueExact(); }
}

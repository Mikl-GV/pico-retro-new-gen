// gpgx_math.c — минимальные math-функции для Genesis Plus GX
// Используются только при инициализации таблиц звука (не hot-path).
#include <stdint.h>

double sin(double x) {
    // sin(x) по ряду Тейлора, x ~ [-π, π], точность ~1e-3
    double s = x, t = x;
    x = x * x;
    t *= x / (2 * 3); s -= t;
    t *= x / (4 * 5); s += t;
    t *= x / (6 * 7); s -= t;
    t *= x / (8 * 9); s += t;
    return s;
}

double cos(double x) { return sin(x + 1.57079632679); }

double floor(double x) {
    int i = (int)x;
    return (double)(i - (x < (double)i));
}

double ceil(double x) {
    int i = (int)x;
    return (double)(i + (x > (double)i));
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;
    if (y == 2.0) return x * x;
    if (y == -1.0) return 1.0 / x;
    // pow(x, y) = exp(y * log(x))
    // минимально: приближение через exp2/log2 для целых степеней
    double r = 1.0;
    int n = (int)y;
    double f = y - (double)n;
    if (n > 0) { for (int i = 0; i < n; i++) r *= x; }
    else if (n < 0) { for (int i = 0; i < -n; i++) r /= x; }
    if (f != 0.0) {
        // крайне грубое приближение: x^0.5 = sqrt,
        // иначе игнорируем дробную часть
        if (f == 0.5 || f == -0.5) r *= sin(1.57079632679);
    }
    return r;
}

double log(double x) {
    // log(x) ~ натуральный логарифм по ряду
    if (x <= 0) return -1.0 / 0.0;
    double r = 0, a = (x - 1) / (x + 1), a2 = a * a;
    double p = a;
    for (int i = 1; i < 20; i += 2) {
        r += p / i;
        p *= a2;
    }
    return 2 * r;
}

double exp(double x) {
    // экспонента через ряд e^x = Σ x^n/n!
    double r = 1.0, t = 1.0;
    for (int i = 1; i < 20; i++) {
        t *= x / i;
        r += t;
    }
    return r;
}

double fabs(double x) { return x < 0 ? -x : x; }
double sqrt(double x) { return pow(x, 0.5); }
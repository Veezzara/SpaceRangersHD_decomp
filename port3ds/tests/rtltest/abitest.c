/* C side of the calling convention test in rtltest.pp. */
typedef struct { double x, y, z; } tv3;
double abitest_hfa(tv3 a, tv3 b, tv3 c, double r) { return a.x + b.y * 10 + c.z * 100 + r * 1000; }
double abitest_many(double a, double b, double c, double d, double e, double f, double g, double h,
                    double i, int k, double j) { return a + b + c + d + e + f + g + h + i * 100 + k * 1000 + j * 10000; }
double abitest_mixed(int a, float b, long long c, double d, float e, int f)
{ return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f * 100000; }
tv3 abitest_ret_hfa(double x) { tv3 r = { x, x * 2, x * 3 }; return r; }

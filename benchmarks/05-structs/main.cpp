#include <cstdio>
#include <cstdint>

struct Vec4 { double a, b, c, d; };
struct Particle { Vec4 pos, vel; int64_t id; };

static double dot(Vec4 p, Vec4 q) {
    return p.a * q.a + p.b * q.b + p.c * q.c + p.d * q.d;
}
static Particle advance(Particle p, double dt) {
    Particle np = p;
    np.pos.a = p.pos.a + p.vel.a * dt;
    np.pos.b = p.pos.b + p.vel.b * dt;
    np.pos.c = p.pos.c + p.vel.c * dt;
    np.pos.d = p.pos.d + p.vel.d * dt;
    return np;
}

int main() {
    Particle p = {{1, 2, 3, 4}, {0.1, 0.2, 0.3, 0.4}, 1};
    double acc = 0;
    for (int64_t i = 0; i < 100000000LL; i++) {
        p = advance(p, 0.01);
        acc += dot(p.pos, p.vel);
    }
    printf("%s\n", acc > 0.0 ? "true" : "false");
    printf("%lld\n", (long long)p.id);
    return 0;
}

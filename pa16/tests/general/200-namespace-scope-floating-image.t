float a = 1.5f;
double b = 2.25;
long double c = 3.5L;
float d = 4;
double e = 1.5f;
float f = 2.25;

struct Pair { float x; double y; };

Pair p = {5.5f, 6.25};
float row[3] = {7.5f, 8.5f, 9.5f};

struct Held { static float m; };
float Held::m = 0.75f;

int main() {
  return (int)(a + b + (double)c + d + e + f + p.x + p.y + row[0] + Held::m) -
         39;
}

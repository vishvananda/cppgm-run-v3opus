typedef int (*unary)(int);
typedef long (*wide)(long);

int one(int a) { return a; }
int two(int a) { return a + 1; }
long three(long a) { return a + 2; }

struct up { operator unary() { return one; } };
struct down : up { operator unary() { return two; } };
struct member : up { int operator()(int a) { return a + 3; } };
struct konst { operator unary() const { return one; } };
struct widened { operator wide() { return three; } };

int hidden(down d) { return d(0); }
int preferred(member m) { return m(1); }
int through_const(const konst& k) { return k(9); }
long converted(widened w) { return w(4); }

int main() { return 0; }

int made = 0;

struct Mark {
  Mark() { made = made + 1; }
  Mark(int, int) { made = made + 10; }
};

struct Holder { Mark m; int k; };

int main() {
  Holder a = { Mark(), 1 };
  Holder b = { {}, 2 };
  Holder c = { {3, 4}, 3 };
  Holder d = { Mark{5, 6}, 4 };
  return made - 12 + a.k + b.k + c.k + d.k - 10;
}

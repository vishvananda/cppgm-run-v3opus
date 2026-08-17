struct made { int value; made(int given) : value(given) {} };

struct quiet
{
  int value;
  quiet(int given) : value(given) {}
  void grow() { quiet other(value); (void)other; }
};

struct host
{
  int value;
  friend made lift(host const& from) { return made(from.value); }
};

int main() { return 0; }

struct YRecord {
  struct { unsigned first; unsigned second; };
  union { unsigned third; unsigned fourth; };
  unsigned fifth;

  unsigned total() { return first + second + third + fifth; }
};

YRecord r;

int main() {
  r.first = 1;
  r.second = 2;
  r.third = 4;
  r.fifth = 8;
  return (int)r.total() + (int)r.fourth + (int)sizeof(YRecord) - 35;
}

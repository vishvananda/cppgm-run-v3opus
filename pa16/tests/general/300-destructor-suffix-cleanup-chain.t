int order;

struct Cell {
  ~Cell() { order = order + 1; }
};

struct Wide {
  Cell m00;
  Cell m01;
  Cell m02;
  Cell m03;
  Cell m04;
  Cell m05;
  Cell m06;
  Cell m07;
  Cell m08;
  Cell m09;
  Cell m10;
  Cell m11;
  Cell m12;
  Cell m13;
  Cell m14;
  Cell m15;
  Cell m16;
};

int main() {
  {
    Wide wide;
  }
  return order == 17 ? 0 : 1;
}

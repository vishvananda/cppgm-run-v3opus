struct YMark {};

struct YHolder : YMark {
  YMark first;
  YMark second;
  int held;
};

YHolder h;

int main() {
  return (int)((char*)&h.first - (char*)&h) +
         (int)((char*)&h.second - (char*)&h) - 3;
}

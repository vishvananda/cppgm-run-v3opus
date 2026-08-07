int sink;

struct YA {
  YA() { sink = sink + 1; }
  ~YA() { sink = sink - 1; }
  int v;
};

struct YX {
  YA w[16];
  YX() { sink = 100; }
  ~YX() { sink = 200; }
};

YX g;

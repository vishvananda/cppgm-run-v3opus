// 9.2p2 regards a class as complete within such things "in nested classes", so
// a member template of a nested class names a member the class around it
// declares below the nest.
struct around {
  struct inner {
    template<class T>
    static int reach() { return below + around::deeper; }
  };

  static const int below = 3;
  static const int deeper = 4;
};

int main() { return around::inner::reach<int>() - 7; }

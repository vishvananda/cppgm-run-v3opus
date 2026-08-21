// 9.2p2: a class is regarded as complete within the function bodies of its own
// member specification "including such things in nested classes", and 8.4p1
// makes a function-body the ctor-initializer and the compound-statement both.
// So the reading of a nested class's member body is made at the `}` that
// completes the class *around* it, and a mem-initializer-list is read there
// with the statements after it - `h.pop<long>(value)` written above
// `template<class U> bool pop(U &)` is 14.2's template-id at either door and
// 5.9's two comparisons where the member specification met the `{`.
//
// The region each reading stands in travels with it, because the class body
// that put it aside is gone by the time the class around it reaches its own
// `}`: what a nested class declared hides what the class around it declared,
// exactly as it does where the body was written.
struct holder {
  struct inner {
    typedef long width;
    static int span() { width measured = 0; return (int)sizeof(measured) - 8; }
    static int run(holder& h, long& value) { return h.pop<long>(value) ? 0 : 1; }
  };

  struct middle {
    struct deep {
      static int run(holder& h, long& value) { return h.pop<long>(value) ? 0 : 1; }
    };
  };

  template<class base> struct wrapped {
    static int run(holder& h, long& value) { return h.pop<long>(value) ? 0 : 1; }
  };

  int held;
  holder(long& value) : held(pop<long>(value) ? 0 : 1) { }

  template<class U, class Enabler = void>
  bool pop(U&) { return true; }

  typedef int width;
};

struct nested_initializer {
  struct member {
    int held;
    member() : held(nested_initializer::pick()) { }
  };
  static int pick() { return 0; }
};

int main() {
  long value = 0;
  holder held(value);
  nested_initializer::member made;
  return held.held + made.held + holder::inner::span() +
         holder::inner::run(held, value) +
         holder::middle::deep::run(held, value) +
         holder::wrapped<int>::run(held, value);
}

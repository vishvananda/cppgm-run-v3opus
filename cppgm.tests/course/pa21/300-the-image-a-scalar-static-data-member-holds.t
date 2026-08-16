// VALIDATION: compile-pass
// N3485 focus: 9.4.2 [class.static.data], 3.6.2 [basic.start.init]
//
// 9.4.2p3 makes the declaration a class writes of a static data member and the
// definition written outside it one declaration, so the initializer the class
// wrote is what initializes the object the definition lays out - whatever the
// member's type is.  A member of arithmetic type would reach 3.6.2p2's image
// anyway, because 5.19p3 folded it to one value; a member whose initializer is
// an *address* has no such value, so a definition that read its own silence as
// 8.5p6's default-initialization would lay out zero and write no initialization
// at all.  Every one of these is read through its address, which is what makes
// 3.2p2 odr-use the object and the image the program's answer.

int helper()
{
  return 6;
}

int target = 3;

template<class = void>
struct table
{
  static constexpr const char *text = "ab";
  static constexpr int (*call)() = &helper;
  static constexpr int *object = &target;
  static constexpr const int *none = nullptr;
  static constexpr double weight = 1.5;
  static constexpr char tag = 'q';
};

template<class T>
constexpr const char *table<T>::text;

template<class T>
constexpr int (*table<T>::call)();

template<class T>
constexpr int *table<T>::object;

template<class T>
constexpr const int *table<T>::none;

template<class T>
constexpr double table<T>::weight;

template<class T>
constexpr char table<T>::tag;

int main()
{
  const char *const *text = &table<>::text;
  int (*const *call)() = &table<>::call;
  int *const *object = &table<>::object;
  const int *const *none = &table<>::none;
  const double *weight = &table<>::weight;
  const char *tag = &table<>::tag;
  return ((*text)[0] == 'a' && (*call)() == 6 && **object == 3 &&
          *none == 0 && *weight > 1.0 && *tag == 'q')
    ? 0 : 1;
}

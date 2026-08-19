// VALIDATION: run-pass
// 14.5.3p1 with 8.3.5p1: a non-type template-parameter is written as a
// parameter-declaration, whose ellipsis stands in the declarator - after every
// ptr-operator and before the declarator-id.  So `int & ... Rs` writes it where
// `int ... Ns` does not, and both declare one place that binds a run.  A place
// whose run the head did not see is one a list of none gives too few arguments
// to, and one 14.8.2.1p1 never binds to the empty run a call with no argument
// leaves it.

int first = 1;
int second = 2;

template<int &... Rs>
struct held { static const unsigned long count = sizeof...(Rs); };

template<int *... Ps>
struct pointed { static const unsigned long count = sizeof...(Ps); };

template<int... Ns>
struct valued { static const unsigned long count = sizeof...(Ns); };

struct check
{
  template<int &...>
  static int unnamed() { return 5; }
};

template<int &... Rs>
int counted() { return static_cast<int>(sizeof...(Rs)); }

int main()
{
  return held<>::count == 0 && held<first, second>::count == 2 &&
      pointed<>::count == 0 && pointed<&first>::count == 1 &&
      valued<>::count == 0 && valued<1, 2, 3>::count == 3 &&
      check::unnamed() == 5 && counted() == 0 && counted<first>() == 1
    ? 0 : 1;
}

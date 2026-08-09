// 7.1.6.3p1 and 3.4.4p2: an elaborated-type-specifier written as a template
// argument names the class the class-key reaches, which an ordinary
// declaration of the same spelling hides from every other lookup.
template<class T> struct wrapper { typedef T type; };

struct Known { static const int value = 7; };
int Known;

wrapper<struct Known> held;

int main() {
  Known = 1;
  return wrapper<struct Known>::type::value == 7 ? 0 : 1;
}

// 4.2p1 and 4.3p1: an array is the address of its first element and a function
// is a pointer to itself, and neither conversion reads a *value* out of the
// object - `static char buf[3];` has an address 5.19p2 gives a constant
// expression and no value 5.19 reads at all.
//
// So a place of pointer type is filled from the object its initializer
// designates, and that is one reading and not one per spelling of 8.5's
// initializers.  Every declaration below writes the same conversion at a
// different one of 8.5's exits: a parenthesized initializer, a braced one, `=`,
// a clause of an aggregate, an element of an array, 12.6.2p8's
// brace-or-equal-initializer, a mem-initializer, 8.3.6p1's default-argument,
// 6.6.3p2's return statement, 5.2.9's cast, and 5.19p3's user-defined
// conversion to a place of pointer type.

static char buf[3];
void fn();
static int target;

struct holder
{
  char *first;
  char *second;
};

struct held
{
  char *inside = buf;
  constexpr held() {}
};

struct built
{
  char *made;
  constexpr built(char *from) : made(from) {}
};

struct source
{
  constexpr operator int *() const { return &target; }
};

// 8.5p16: the three spellings of one initialization.
constexpr char *paren_init(buf);
constexpr char *braced_init{buf};
constexpr char *equals_init = buf;

// 8.5.1p2: a clause of an aggregate and an element of an array.
constexpr holder aggregate = { buf, buf };
constexpr char *elements[2] = { buf, buf };

// 12.6.2p8 and 12.6.2p2: the initializer the member's own declaration wrote,
// and the one a constructor's mem-initializer writes.
constexpr held brace_or_equal;
constexpr built mem_initialized(buf);

// 4.3p1: a function name reaching a place of pointer to function type.
constexpr void (*function_pointer)() = fn;

// 8.3.6p1: the default-argument stands where the argument is missing.
constexpr char *defaulted(char *from = buf) { return from; }
constexpr char *by_default = defaulted();

// 6.6.3p2: the return statement initializes what the call hands back.
constexpr char *returned() { return buf; }
constexpr char *from_return = returned();

// 5.2.9p4: a cast direct-initializes an object of the type named.
constexpr char *cast_init = static_cast<char *>(buf);

// 5.19p3 with 12.3.2p1: a converted constant expression may reach its place
// through a user-defined conversion, and a place of pointer type is one.
constexpr int *through_conversion = source();

int main()
{
  return *paren_init + *braced_init + *equals_init + *aggregate.first +
    *aggregate.second + *elements[0] + *elements[1] + *brace_or_equal.inside +
    *mem_initialized.made + *by_default + *from_return + *cast_init +
    *through_conversion + (function_pointer == 0 ? 1 : 0);
}

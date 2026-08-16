// 5.19p2: an address constant expression is a prvalue of pointer type that
// evaluates to the address of an object with static storage duration, to the
// address of a function, or to a null pointer value.  What such a constant is
// worth is therefore *which object* it designates and never a number: 5.10p1's
// equality is a question about the objects, 5.7p5's arithmetic walks the array
// one designates an element of, and 5.3.1p1's `*` reads what that object holds.
//
// The four objects an address here designates are the ones the language gives
// storage of its own: a declaration the program wrote, a subobject of one,
// 2.14.5p8's string literal that no declaration named, and a function.

// 3.9.2p1 and 5.3.1p3: the address of an object, which is a constant expression
// even where reading that object is not one - the storage exists whatever the
// program later writes into it.
int mutable_scalar = 0;
constexpr int *scalar_address = &mutable_scalar;
static_assert(scalar_address == &mutable_scalar, "one object, one address");
static_assert(scalar_address != nullptr, "4.10p1 tells it from the null value");
static_assert(scalar_address, "4.12p1 asks only whether it designates one");

// 2.14.5p8 with 4.2p1: the literal is an array object, and a pointer place
// takes the address of its first element.
constexpr const char *text = "ab";
static_assert("ab"[1] == 'b', "5.2.1p1 names an element of that object");
static_assert(*(text + 2) == 0, "2.14.5p12 appended the terminating null");

// 8.3.4p6 and 5.7p5: an array a declaration wrote, walked by the same rules.
constexpr int values[3] = {4, 5, 6};
constexpr const int *first = values;
static_assert(*first == 4, "4.2p1 decays to the first element");
static_assert(*(first + 2) == 6, "5.7p5 walks the array");
static_assert(first + 3 != first, "5.7p4's one past the last element exists");
static_assert(&values[1] == first + 1, "the two spellings name one object");

// 9.2p13's member is a subobject of the object that holds it, and 5.2.5p1's two
// forms reach it through an object and through a pointer alike.
struct point
{
  int x;
  int y;
};

constexpr point origin = {7, 9};
constexpr const point *located = &origin;
static_assert(&located->y == &origin.y, "5.2.5p2 makes `->` `(*E).`");
static_assert(located->y == 9, "and reads what that subobject holds");

// 8.3.2p1 and 12.2p1: a reference binds to the object the argument designates,
// so a body that hands its address back hands back the argument's own.  A
// member function reached on a constant object binds its members the same way,
// which is what makes the address a `return &member;` produces the caller's.
constexpr const int *address_of(const int &value)
{
  return &value;
}

struct holder
{
  int cell[2];

  constexpr const int *data() const { return cell; }
  constexpr const int &at(unsigned index) const { return cell[index]; }
};

constexpr holder pair = {{1, 2}};
static_assert(address_of(origin.x) == &origin.x, "the argument's own object");
static_assert(pair.data() == &pair.cell[0], "9.2p1's member of *this*");
static_assert(&pair.at(1) == pair.data() + 1, "8.3.2p1 hands the object back");

// 9.2p13 and 8.3.4p6: an address is a subobject's value like any other, so a
// member and an element hold one exactly as they hold a number - and 3.6.2p2's
// image of such an object carries the symbol beside the item.
struct boxed
{
  const int *cell;
  int count;
};

constexpr boxed carried = {&mutable_scalar, 5};
constexpr const int *carried_addresses[2] = {&mutable_scalar, nullptr};
static_assert(carried.cell == &mutable_scalar, "a member holds an address");
static_assert(carried.count == 5, "beside a member that holds a number");
static_assert(carried_addresses[0] == carried.cell, "an element holds one too");
static_assert(carried_addresses[1] == nullptr, "and 4.10p1's null value");

// 4.3p1: a function name designates the function, and a call written through
// the pointer it decays to runs that declaration.
constexpr int twice(int value)
{
  return value + value;
}

constexpr int through(int (*called)(int), int value)
{
  return called(value);
}

static_assert(through(&twice, 4) == 8, "5.2.2p1 through a pointer");
static_assert(through(twice, 5) == 10, "which 4.3p1's decay also writes");

int main()
{
  return 0;
}

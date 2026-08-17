// VALIDATION: compile-pass
// N3485 focus: 4.2 [conv.array], 5.7 [expr.add], 5.19 [expr.const]
//
// 4.2p1 converts an lvalue of array type to a pointer to its first element, and
// that conversion is the one thing every operand position does to such a name
// before it looks at it.  A name of array type is never read as a *value*:
// `int numbers[4];` outside a constant expression holds none, and none of the
// operators below waits on one.
//
// So what the name is worth to a fold is which object it is, exactly as a
// `static int n;` is - and each reader applies 4.2p1 for itself.  5.7p5's
// arithmetic, 5.9p2's ordering, 5.10p1's equality, 4.12p1's contextual `bool`,
// 5.3.1p1's `*` and 5.2.1p1's subscript are one sentence asked at one door, and
// 8.3.2p1's binding of a reference to the array reads the same object without
// the conversion at all.
//
// What is still no constant expression is the *value* an element holds, which
// the array being non-const is what settles - so `numbers[0]` is refused where
// `&numbers[0]` is not, and that boundary is unmoved by any of it.

int numbers[4] = {1, 2, 3, 4};
int other[4] = {5, 6, 7, 8};

// 5.7p5: the address arithmetic 4.2p1's pointer is written on, either way
// round.
constexpr int* stepped = numbers + 1;
constexpr int* mirrored = 1 + numbers;
constexpr int* element = &numbers[2];
static_assert(stepped == mirrored, "one element in");
static_assert(element == numbers + 2, "two elements in");

// 5.10p1: two pointers designate the same object or they do not.
static_assert(numbers == numbers, "the same array");
static_assert(numbers != other, "two arrays");

// 4.12p1: the conversion 5.3.1p9's `!` and 5.16p1's condition ask for is 4.2p1's
// pointer taken as a truth, which the first element of an array always has.
static_assert(!numbers == false, "an array is never null");
static_assert((numbers ? 1 : 0) == 1, "a condition reads the same pointer");

// 5.2.1p1: the element the subscript names, whose address is a constant however
// little 5.19 knows of what it holds.
static_assert(&numbers[3] == numbers + 3, "the last element");

// 8.3.2p1: a reference bound to the array is the object itself, with no 4.2p1
// anywhere in it - and the decay written inside the body is the same one.
constexpr int* first_of(int (&given)[4])
{
  return given;
}
static_assert(first_of(numbers) == numbers, "bound to the array");
static_assert(first_of(other) == other, "and to the other one");

// 9.4.2p3 with 5.19p2: the same sentence read where a class initialized the
// member, which is a constant the program knows and no storage it lays out.
struct holder
{
  static constexpr int* chosen = numbers + 2;
  static constexpr int* last = other + 3;
};
static_assert(holder::chosen == &numbers[2], "the member's own address");
static_assert(holder::last == other + 3, "and the other array's");

int read_int(const int* at)
{
  return *at;
}

int main()
{
  int total = 0;
  total += read_int(stepped);
  total += read_int(mirrored);
  total += read_int(element);
  total += read_int(holder::chosen);
  total += read_int(holder::last);
  return total == 0 ? 0 : 1;
}

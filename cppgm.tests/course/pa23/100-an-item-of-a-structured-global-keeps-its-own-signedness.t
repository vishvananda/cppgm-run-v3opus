// VALIDATION: compile-pass
// `lowir.md` names no `u64`, so 3.9.1p2's two eight-byte integral types are
// both `i64` and a value is spelled with the digits that type can be written
// with.  That is the answer for the operand of `global @x : i64 = v`, which
// names the whole storage of a scalar object - `dynamic_extent` below is
// `= -1` and no wider spelling of the same bits.
//
// An *item* of a structured global is the other half of 3.6.2p2 and is not
// that operand: it stands for one clause of the image the program wrote, and
// what it carries is that clause's own digits.  `18446744073709551615UL`
// written as an element is `i64 18446744073709551615` there, so the two paths
// ask two questions and one function may not answer both.

static const unsigned long dynamic_extent = (unsigned long)-1;

unsigned long extents[3] = { 1UL, 18446744073709551615UL, 2UL };

struct spans
{
  unsigned long first;
  long second;
  unsigned int third;
};

spans written = { 18446744073709551615UL, -2, 4294967295u };

int main()
{
  return (extents[1] == dynamic_extent && written.first == dynamic_extent &&
          written.second == -2 && written.third == 4294967295u)
    ? 0 : 1;
}

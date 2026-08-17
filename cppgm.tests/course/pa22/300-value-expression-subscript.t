// VALIDATION: compile-pass
// N3485 focus: 5.2.1 [expr.sub], 14.2 [temp.names], 5.19 [expr.const]
//
// 14.2 writes a template-argument-list inside a name, so an argument reaches
// the semantic layer as text and every operator 5.19 has must be read out of
// the words as well as out of the tree.  5.2.1p1's subscript is one of them,
// and its three left operands - an array a name designates, a pointer into one
// and an object whose class declares `operator[]` - are one reading both doors
// share.

typedef unsigned long size_t;

constexpr size_t sizes[] = {13ul, 29ul, 53ul};
constexpr size_t grid[2][3] = {{1ul, 2ul, 3ul}, {4ul, 5ul, 6ul}};

struct bag
{
	size_t held[3];

	// 13.5.5p1: a subscript of an object of class type is a call of this, which
	// the reading over the words asks for in the order the program wrote it.
	constexpr size_t operator[](size_t at) const
	{
		return held[at] * 10ul;
	}
};

constexpr bag scaled = {{1ul, 2ul, 3ul}};

struct point
{
	size_t x;
};

constexpr point points[2] = {{7ul}, {8ul}};

struct holder
{
	size_t members[3];
};

constexpr holder kept = {{4ul, 5ul, 6ul}};

template<size_t N>
struct box
{
	static const size_t value = N;
};

// 8.3.4p6: the element a name designates.
typedef box<sizes[2]> from_array;
// The same one level down, which is an element of an element.
typedef box<grid[1][2]> from_matrix;
// 2.14.5p8: the one object 5.19 reads out of storage that no declaration named,
// which 5.1.1p6's parentheses around it do not change.
typedef box<"abcd"[2]> from_literal;
typedef box<("abcd")[3]> from_parenthesized_literal;
// One subscript written inside another's index.
typedef box<sizes[sizes[0] % 3ul]> from_nested_index;
// 13.5.5p1 through the words.
typedef box<scaled[2]> from_operator;
// 5.2.5p1 before the subscript, and after it.
typedef box<kept.members[1]> from_member_array;
typedef box<points[1].x> from_array_member;

// 14.6p8: the index is a place until an argument list settles it, and the
// element it names is the argument's own to say.
template<size_t Index>
struct pick
{
	static const size_t value = box<sizes[Index]>::value;
};

int main()
{
	return from_array::value == 53ul && from_matrix::value == 6ul &&
	       from_literal::value == 'c' &&
	       from_parenthesized_literal::value == 'd' &&
	       from_nested_index::value == 29ul && from_operator::value == 30ul &&
	       from_member_array::value == 5ul && from_array_member::value == 8ul &&
	       pick<0>::value == 13ul && pick<2>::value == 53ul ? 0 : 1;
}

// VALIDATION: compile-pass
// N3485 focus: 7.1.6.2 [dcl.type.simple], 14.2 [temp.names], 14.8.1 [temp.arg.explicit]
//
// 14.2 leaves a template-id naming the specializations its argument list makes
// and no declaration bound to the whole spelling, so every reading that looks
// one name up with no overload set to hand on has to ask the template layer
// first.  7.1.6.2p4 is one of those: it asks what an id-expression *names*, and
// 5p8 leaves the operand unevaluated, so the naming is 3.2p2's use of nothing.
//
// 14.8.1p2 is what says how large the set such a reading chooses from is: a
// declaration the written list did not complete is one a deduction still has to
// finish, and a naming with neither a call nor a target type makes none.

typedef int (*fn_type)();

template<class T>
int solo()
{
	return 1;
}

// The written list fills this head's first place and leaves `U` for a deduction
// that never comes, so `solo<char>` names the declaration above and not this.
template<class T, class U>
int solo()
{
	return 2;
}

struct plain
{
	template<class T>
	static int member()
	{
		return 4;
	}
};

template<class T>
struct holder
{
	template<class U>
	static int nested()
	{
		return 8;
	}
};

// 7.1.6.2p4 at the three spellings a template-id is written in.
typedef decltype(solo<char>) free_named;
typedef decltype(plain::member<char>) member_named;
typedef decltype(holder<long>::template nested<char>) nested_named;

// The same reading standing where a type-id belongs, which is what says the
// specifier named a function type and not a declaration.
free_named* const addressed = &solo<char>;
member_named* const from_member = &plain::member<char>;
nested_named* const from_nested = &holder<long>::template nested<char>;

// 14.8.1p2 with 5.3.1p3: the naming a constant expression makes chooses from
// the same set, so the head the list left unfilled is no member of it there
// either.
constexpr fn_type folded = &solo<char>;

template<class T>
struct table
{
	// 7.1.6.2p4 read where the pattern stands and again for each
	// specialization, over an argument the enclosing head declared.
	typedef decltype(solo<T>) entry;

	static int through()
	{
		entry* const held = &solo<T>;
		return held();
	}
};

int main()
{
	const int reached =
		addressed() + from_member() + from_nested() + folded();
	return reached == 1 + 4 + 8 + 1 && table<long>::through() == 1 ? 0 : 1;
}

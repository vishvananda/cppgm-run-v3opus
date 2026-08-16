// 14.7.3p1: an explicit specialization is a declaration the program wrote out
// for one argument list, and where it carries a body that body is a definition
// this translation unit owns.  A template with no pattern of its own still has
// the specializations written out for it: nothing is instantiated there, and
// the definition is still the program's.
//
// 14.7.3p6 then says what binds it: such a declaration is `inline` only where
// its own decl-specifiers say so, so the constexpr one below is a definition
// every unit that needs one may hold and the plain one is the program's single
// definition.  The specialization a *pattern* was read again for is the other
// way round - that one belongs to the program wherever the program reaches it,
// whatever the pattern's own specifiers said.

template<class T>
int declared_only();

template<>
int declared_only<int>()
{
	return 2;
}

template<class T>
constexpr int declared_constexpr();

template<>
constexpr int declared_constexpr<int>()
{
	return 3;
}

static_assert(declared_constexpr<int>() == 3,
              "a constexpr explicit specialization folds where it is named");

template<class T>
int with_pattern(T value)
{
	return value + 4;
}

template<>
int with_pattern<char>(char value)
{
	return value + 8;
}

template<class T>
constexpr T from_pattern(T value)
{
	return value * 2;
}

static_assert(from_pattern(5) == 10,
              "a specialization read from the pattern folds too");

int main()
{
	return declared_only<int>() + declared_constexpr<int>() +
	       with_pattern(1) + with_pattern('\0') + from_pattern(3) - 26;
}

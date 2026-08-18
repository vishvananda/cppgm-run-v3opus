// 7.1.5p2 stands on the template's declarator and travels to every
// specialization the program did not write out: a reading that makes no
// definition of one has no body to read the answer off, so it asks the
// template.  14.7.3p1's explicit specialization is the exception - that is a
// declaration of its own, and its own decl-specifiers are what say whether a
// call of it is one 5.19 reads.
//
// `weigh<char>` is the first: nothing declares it but the naming, and it folds
// because `weigh` was written `constexpr`.  `weigh<int>` is the second: the
// program wrote it out without `constexpr`, so the condition below is not a
// constant expression however plainly the template it specializes was one.

template<class T>
constexpr int weigh()
{
	return (int)sizeof(T);
}

template<>
int weigh<int>()
{
	return 0;
}

// The specialization no declaration but the naming makes still folds, which is
// 7.1.5p2 read off the template.
static_assert(weigh<char>() == 1, "a specialization of a constexpr template is a constexpr function");

// The one 14.7.3p1 let the program write out does not.
static_assert(weigh<int>() == 0, "an explicit specialization says for itself whether it is constexpr");

int main()
{
	return weigh<int>();
}

// 15.4p1's condition is a constant expression, and 9.2p2 makes the
// exception-specification a context the class it stands in is complete in - so
// the fold is made at the `}` and not at the declarator that wrote it.  A
// condition naming a member declared below folds to nothing where the
// declarator stands, and a specification that folds to nothing is one 15.4p14
// leaves throwing: `settled()` is `noexcept` here and may throw where the fold
// is made too early.
//
// The door is the one a member *declaration* already went through.  A member
// written with its definition reaches a walk of its own, and that walk kept no
// condition for the closing brace, so the two spellings of one member answered
// 15.4p1 differently.
struct held
{
	static held * ptr;

	static int settled() noexcept(sizeof(ptr->width) == 4) { return 0; }

	static int declared() noexcept(sizeof(ptr->width) == 4);

	int width;
};

int held::declared() noexcept { return 0; }

held * held::ptr = 0;

int main()
{
	return held::settled() + (noexcept(held::settled()) ? 0 : 1) +
	       held::declared() + (noexcept(held::declared()) ? 0 : 1);
}

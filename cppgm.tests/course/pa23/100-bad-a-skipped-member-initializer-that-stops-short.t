// The same sentence at 9.2's brace-or-equal-initializer: the reading is made at
// the `}` from a range of its own, so nothing after it refuses what it did not
// read.  `3 4` would otherwise pass as `3` with the second terminal dropped.
struct holder
{
	int held = 3 4;
};

int main()
{
	holder object;
	return object.held;
}

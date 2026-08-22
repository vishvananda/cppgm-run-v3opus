// 14.2p1 builds a template-id on three names and an identifier is only one of
// them, so a range 9.2p2 puts aside is scanned for its end by a rule that knows
// all three: `operator+<int, long>` writes a comma inside a
// template-argument-list where the scan is looking for the comma that ends a
// default argument, and no identifier stands before the `<` that opens it.
// Both places stand where the class is complete, so both name a nested class
// declared below the member that writes them.
struct holder
{
	static int made(int width = target().operator+<int, long>(2))
	{
		return width;
	}

	int held = target().operator+<char, int>(4);

	static bool settled() noexcept(sizeof(target().operator+<int, long>(0)) == 4)
	{
		return true;
	}

	struct target
	{
		template<class T, class U> int operator+(int k) const
		{
			return k + 3;
		}
	};
};

int main()
{
	holder object;
	return holder::made() - 5 + object.held - 7 + (holder::settled() ? 0 : 1) +
	       (noexcept(holder::settled()) ? 0 : 1);
}

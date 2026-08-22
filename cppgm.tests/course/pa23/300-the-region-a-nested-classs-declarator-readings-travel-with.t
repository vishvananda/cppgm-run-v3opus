// 9.2p2 in a nested class: the reading is made at the `}` that completes the
// *outermost* class of a nest, so the region the nested class gave its members
// travels with each of the three readings its member specification put aside -
// the same way a member function body's already does.
//
// The head a member template was written under travels with them too: the
// places 14.1p2 declared go out of scope with the declarator, and a default
// argument written inside one names them.
struct outer
{
	struct held
	{
		static held * ptr;

		static int made(int width = (int)sizeof(ptr->pop<long>()))
		{
			return width;
		}

		int kept = (int)sizeof(ptr->pop<long>()) + width;

		template<class W> static int wide(int span = (int)sizeof(W))
		{
			return span;
		}

		template<class U> bool pop() { return true; }
		static const int width = 3;
	};

	static int span() { return held::made(); }
};

outer::held * outer::held::ptr = 0;

int main()
{
	outer::held object;
	return outer::span() - 1 + object.kept - 4 + outer::held::wide<long>() - 8;
}

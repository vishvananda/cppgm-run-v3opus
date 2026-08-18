// 14.1p4's fifth place is `std::nullptr_t`, whose one value is the one
// 14.3.2p1's last bullet names an address constant expression of - and 13.4p1's
// target chooses one declaration of an overloaded name at 14.1p4's second, with
// or without the `&` 14.3.2p1 lets a function name omit.
static int counted = 3;

int hint(int) { return counted + 1; }
int hint() { return counted; }

template<decltype(nullptr) N>
struct one
{
	static int only() { return counted; }
};

template<int (*F)()>
struct called
{
	static int twice() { return F() + F(); }
};

int main()
{
	return one<nullptr>::only() + called<hint>::twice() +
		called<&hint>::twice() - 15;
}

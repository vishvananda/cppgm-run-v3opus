// VALIDATION: compile-pass
// 14.6.4.2p1 read over 3.3.1p1: the number a namespace-bound declaration
// carries says where the *name* came into scope in that region, so a reading a
// pattern left standing finds every name that had been declared where it was
// written.  A second declaration of a name the region already declares stands
// where the first one did - `extern int anchor;` above a pattern and
// `int anchor = 4;` below it are one object, and it is the second binding the
// lookup reads.  Numbering that one where it was written puts a name the
// pattern could see out of its own reach.
//
// Which of an *overloaded* name's declarations a second reading may reach is
// 13.1's walk of the chain and is unchanged by this: it reads each
// declaration's own number, so the `pick(int)` below the pattern still joins
// no set the pattern's reading gathers.

int chosen = 0;

void pick(long)
{
	chosen += 1;
}

// 3.1p2: a declaration that is no definition, above every pattern that names
// what it declares.
extern int anchor;

struct box;

int helper(int);

template<class T>
int run(T value)
{
	pick(value);
	return helper(value) + anchor;
}

template<class T>
int held(T *of)
{
	return of->kept;
}

void pick(int)
{
	chosen += 100;
}

int anchor = 4;

struct box
{
	int kept;
};

int helper(int value)
{
	return value + 1;
}

box one = {3};

int main()
{
	return run(1) + held(&one) + chosen == 1 + 1 + 4 + 3 + 1 ? 0 : 1;
}

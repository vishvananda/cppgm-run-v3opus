// 14.6.4.2p1 read over 3.3p4, which is the other half of the sentence
// `100-a-name-a-later-declaration-rebinds-stood-where-the-first-did` writes.
// A second declaration of a name the region already declares stands where the
// first one did only where the two declare the same thing - `extern int x;` and
// `int x = 4;` are one object, and the later binding is what the lookup reads.
//
// A declaration that binds one spelling to another *kind* of entity is a
// declaration of its own however the region already spelled the name.  `struct
// probe;` above the pattern and `int probe(int);` below it are two declarations
// of one spelling, and what the pattern's reading finds is the class: `probe(v)`
// is 5.2.3p1's functional cast of an incomplete type and no call, so the program
// is ill formed and the function below is not what saves it.
//
// Numbering the function where the class stood would put it in the pattern's
// reach and translate this program to 8.

struct probe;

template<class T>
int run(T value)
{
	return probe(value);
}

int probe(int value)
{
	return value + 7;
}

int main()
{
	return run(1) == 8 ? 0 : 1;
}

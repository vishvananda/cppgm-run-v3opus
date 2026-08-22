// A construct read where it stands is refused by whatever the rule around it
// wrote next.  9.2p2's other three contexts are skipped to a delimiter and read
// again at the `}`, so there is no rule behind the reading and the run the skip
// recorded is what holds it: a reading that stops a token short leaves
// terminals the program wrote that nothing reads, and `1 2` is a default
// argument that would otherwise pass as `1`.
struct holder
{
	static int made(int width = 1 2)
	{
		return width;
	}
};

int main()
{
	return holder::made();
}

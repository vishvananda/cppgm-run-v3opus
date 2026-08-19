// 8.5.1p7 with 3.6.2p2: an element of an object with static storage duration
// that a written initializer-list did not reach is value-initialized, which the
// image holds as one item at the element's own type - where an object no
// initializer reached at all holds one run of zero bytes instead.
int written_none[4] = {};
int written_one[4] = { 1 };
int written_all[2] = { 1, 2 };
int written_nothing[4];
char narrow_none[3] = {};
char narrow_one[3] = { 'a' };
short widened[5] = { 1 };
int nested[2][3] = { { 1 } };
int main()
{
  return written_none[3] + written_one[3] + written_all[1] - 2 +
         written_nothing[3] + narrow_none[2] + narrow_one[2] +
         widened[4] + nested[1][2];
}

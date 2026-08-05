// The phase 7 type of a literal decides whether 16.2.4 widens it to intmax_t
// or to uintmax_t, which is what the operators below see.
2147483647
2147483648
4294967295
4294967296
2147483648 < 0
4294967295u < 0
0x80000000
0x100000000
0xffffffff < 0
0xffffffffffffffff < 0
-1 < 1u
-1 < 1
'\xff'
'a' + -1
u'a' + -1
U'a' + -1
L'a' + -1
-1 % u'a'
-1 % L'a'
-1 / U'a'
-1 / 'a'
true + -2

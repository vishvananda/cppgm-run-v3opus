// An operand that short circuiting or a conditional does not evaluate is still
// parsed and still typed, but the errors of its operators are not reported.
0 && 1/0
0 && (1 && 1/0)
1 || (0 || 1/0)
1 ? 1 : (1/0 + 1u)
0 ? 1/0 : 2
0 ? 1 : 1/0
1 ? 0 ? 1/0 : 2 : 3
0 ? 1 : 0 ? 1/0 : 4
1 && (0 ? 1/0 : 5)
(0 && 1/0) ? 6 : 7u
0 ? 1/0u : -5
1 ? -5 : 1/0u
0 ? 5u : 5
(0 ? 1u : 1) + -1
1 || 1 << 64
1 && 1 << 64

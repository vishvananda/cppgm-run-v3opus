// The 2.13 operators spelled as identifiers are preprocessing-op-or-punc in
// phase 3, so a controlling expression sees them as operators and not as the
// identifiers the `identifier_or_keyword` production would evaluate as 0.
2 not_eq 3
2 not_eq 2
not_eq 2
2 and_eq 3
2 or_eq 3
2 xor_eq 3
new
delete
2 + new
defined and
defined new
compl 0 bitand 7
not not 5
not 0 or not 1
1 bitor 2 xor 3 bitand 6

// A universal-character-name is what phase 1 leaves a character outside the
// basic source character set spelled as, so re-lexing a joined spelling still
// reads one: `\` ## `u0041` is the identifier it designates.
#define cat(a,b) a ## b
cat(\,u0041)

# An `index` offset carries its own type, not the element type of the
# instruction, so a narrow offset must be read at its own width.
function @main() -> i64 {
  slot $cells : obj<64x8>

  block ^entry:
    %base = addr $cells
    %narrow = const i32 2
    %slot2 = index i64 %base, %narrow
    %seven = const i64 7
    store i64 %seven, %slot2
    %back = const i64 2
    %again = index i64 %base, %back
    %value = load i64 %again
    return i64 %value
}

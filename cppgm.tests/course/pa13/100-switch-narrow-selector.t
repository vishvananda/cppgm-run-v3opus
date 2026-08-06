# A switch selector and its case values each carry their own type; they are
# compared as widened canonical integers rather than at the narrow width.
function @main() -> i64 {
  block ^entry:
    %selector = const i8 1
    %arm = const i32 1
    switch %selector, ^fallback, %arm:^hit
  block ^hit:
    %hit_value = const i64 3
    return i64 %hit_value
  block ^fallback:
    %miss_value = const i64 9
    return i64 %miss_value
}

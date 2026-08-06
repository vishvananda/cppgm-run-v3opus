# The `u...` predicates only mean something where signedness is a distinction.
function @main() -> i64 {
  block ^entry:
    %a = const f64 1.5
    %b = const f64 2.5
    %ok = cmp ult f64 %a, %b
    return i64 %ok
}

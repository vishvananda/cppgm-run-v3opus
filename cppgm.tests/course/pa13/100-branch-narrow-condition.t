# A branch condition carries its own type and is widened before the zero test.
function @main() -> i64 {
  block ^entry:
    %flag = const i8 1
    branch %flag, ^taken, ^skipped
  block ^taken:
    %taken_value = const i64 4
    return i64 %taken_value
  block ^skipped:
    %skipped_value = const i64 5
    return i64 %skipped_value
}

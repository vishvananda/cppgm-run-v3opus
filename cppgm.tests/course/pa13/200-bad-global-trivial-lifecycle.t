# `trivial_lifecycle` is function-only metadata.
global @value : i64 [trivial_lifecycle=yes] = 1

function @main() -> i64 {
  block ^entry:
    return i64 0
}

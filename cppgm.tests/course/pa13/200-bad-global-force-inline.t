# `force_inline` is function-only metadata.
global @value : i64 [force_inline=yes] = 1

function @main() -> i64 {
  block ^entry:
    return i64 0
}

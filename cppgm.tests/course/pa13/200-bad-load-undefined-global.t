# A read of a declared-but-undefined global has no label to name either.
declare global @state : i64

function @main() -> i64 {
  block ^entry:
    %v = load i64 @state
    return i64 %v
}

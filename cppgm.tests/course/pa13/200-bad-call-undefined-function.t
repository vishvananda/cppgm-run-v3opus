# CY86 has no linker: a call whose target is only declared cannot be emitted.
declare function @helper() -> i64

function @main() -> i64 {
  block ^entry:
    %r = call i64 @helper()
    return i64 %r
}

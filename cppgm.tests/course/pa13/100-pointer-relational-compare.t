# Signedness lives in the predicate spelling, so `lt`/`le`/`gt`/`ge` are the
# signed comparisons for pointer operands too.
function @main() -> i64 {
  slot $first : i64
  slot $second : i64

  block ^entry:
    %a = addr $first
    %b = addr $second
    %lt = cmp lt ptr %a, %b
    %le = cmp le ptr %a, %b
    %gt = cmp gt ptr %a, %b
    %ge = cmp ge ptr %a, %b
    %ult = cmp ult ptr %a, %b
    %s1 = binary add i64 %lt, %le
    %s2 = binary add i64 %s1, %gt
    %s3 = binary add i64 %s2, %ge
    %s4 = binary add i64 %s3, %ult
    return i64 %s4
}

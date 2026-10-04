// tinylib: ASCII <-> i64 conversion (Core side)
// format_buf: decimal digits of v (>= 0) into p, returns digit count.
// parse_buf: n digit chars -> i64.
pub func parse_buf(p: ptr<char>, n: usize) -> i64 {
    mut v: i64 = 0
    mut i: usize = 0
    while i < n {
        v = v * 10 + (p[i] - '0') as i64
        i += 1
    }
    return v
}

pub func format_buf(p: ptr<char>, v: i64) -> usize {
    if v == 0 {
        p[0] = '0'
        return 1
    }
    mut cnt: usize = 0
    mut mm = v
    while mm > 0 {
        cnt += 1
        mm = mm / 10
    }
    mut pos = cnt
    mut m = v
    while m > 0 {
        pos -= 1
        p[pos] = '0' + (m % 10) as char
        m = m / 10
    }
    return cnt
}

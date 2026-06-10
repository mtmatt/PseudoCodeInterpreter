#[inline(never)]
fn mix(x: i64) -> i64 {
    (x * 3 + 7) % 1_000_003
}

fn main() {
    let n: i64 = 60_000;
    let mut acc: i64 = 1;
    for i in 1..=n {
        acc = mix(acc + i);
    }
    println!("{acc}");
}

fn main() {
    let n: usize = 50_000;
    let mut arr = Vec::new();
    for i in 1..=n {
        arr.push(((i as i64) * 19) % 997);
    }

    let mut total: i64 = 0;
    for _ in 1..=n {
        total = (total + arr.pop().unwrap()) % 1_000_000_007;
    }
    println!("{total}");
}

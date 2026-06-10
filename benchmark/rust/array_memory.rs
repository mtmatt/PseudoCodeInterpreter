fn main() {
    let n: usize = 50_000;
    let mut arr = vec![0_i64; n];
    for i in 1..=n {
        arr[i - 1] = ((i as i64) * 13) % 1009;
    }

    let mut total: i64 = 0;
    for i in 1..=n {
        total = (total + arr[i - 1]) % 1_000_000_007;
    }
    println!("{total}");
}

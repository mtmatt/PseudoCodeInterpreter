fn main() {
    let n: i64 = 200_000;
    let mut acc: i64 = 0;
    for i in 1..=n {
        acc = (acc + (i * 17) % 97) % 1_000_000_007;
    }
    println!("{acc}");
}

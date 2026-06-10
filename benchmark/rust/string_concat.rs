fn main() {
    let n: usize = 20_000;
    let mut s = String::new();
    for _ in 1..=n {
        s.push('x');
    }
    println!("{n}");
}

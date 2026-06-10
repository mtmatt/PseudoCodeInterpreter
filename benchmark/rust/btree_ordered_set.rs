use std::collections::BTreeSet;

fn main() {
    let n: i64 = 1_200;
    let mut tree = BTreeSet::new();

    for i in 1..=n {
        let value = (i * 7919) % 100_000;
        tree.insert(value);
    }

    let mut hits = 0;
    for i in 1..=n {
        let value = (i * 3571) % 100_000;
        if tree.contains(&value) {
            hits += 1;
        }
    }

    println!(
        "{} {} {} {} 3",
        tree.len(),
        tree.first().unwrap(),
        tree.last().unwrap(),
        hits
    );
}

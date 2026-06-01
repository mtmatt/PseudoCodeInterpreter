n = 200000
acc = 0
for i in range(1, n + 1):
    acc = (acc + (i * 17) % 97) % 1_000_000_007
print(acc)

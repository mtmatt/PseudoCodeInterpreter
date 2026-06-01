def mix(x):
    return (x * 3 + 7) % 1_000_003


n = 60000
acc = 1
for i in range(1, n + 1):
    acc = mix(acc + i)
print(acc)

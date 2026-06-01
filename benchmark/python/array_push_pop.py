n = 50000
arr = []
for i in range(1, n + 1):
    arr.append((i * 19) % 997)

total = 0
for _ in range(1, n + 1):
    total = (total + arr.pop()) % 1_000_000_007
print(total)

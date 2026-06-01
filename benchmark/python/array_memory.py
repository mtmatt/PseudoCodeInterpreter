n = 50000
arr = [None] * n
for i in range(1, n + 1):
    arr[i - 1] = (i * 13) % 1009

total = 0
for i in range(1, n + 1):
    total = (total + arr[i - 1]) % 1_000_000_007
print(total)

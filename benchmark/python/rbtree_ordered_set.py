from bisect import bisect_left


class OrderedSet:
    def __init__(self):
        self.data = []

    def insert(self, value):
        index = bisect_left(self.data, value)
        if index == len(self.data) or self.data[index] != value:
            self.data.insert(index, value)

    def contains(self, value):
        index = bisect_left(self.data, value)
        return index < len(self.data) and self.data[index] == value

    def min(self):
        return self.data[0]

    def max(self):
        return self.data[-1]

    def size(self):
        return len(self.data)


n = 1200
tree = OrderedSet()

for i in range(1, n + 1):
    value = (i * 7919) % 100000
    tree.insert(value)

hits = 0
for i in range(1, n + 1):
    value = (i * 3571) % 100000
    if tree.contains(value):
        hits += 1

print(tree.size(), tree.min(), tree.max(), hits, "black")

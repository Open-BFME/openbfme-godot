"""Independent model of the BFME2 1.06 AptNativeHash (0x00B0AC90 Set, 0x00B0AF90 Find, 0x00B0B2C0 Remove,
0x00B0ABC0 resize, 0x00B0AA40/AA0 enumeration) written from the disassembly, not from the C++ port."""
import itertools


def h16(name):
    x = 0x811c9dc5
    for b in name.encode('latin1'):
        c = b - 256 if b > 127 else b
        if 0x41 <= c <= 0x5a:
            c += 0x20
        x = ((c ^ x) * 0x1000193) & 0xffffffff
    x &= 0xffff
    return x or 0x4567


class Tab:
    def __init__(self, size=8):
        self.size = size
        self.k = [None] * size   # None = never used, '' = tombstone
        self.v = [None] * size

    def win(self, idx):
        lo = idx - 8
        if lo < 0:
            return 0, (16 if self.size > 16 else self.size - 1)
        hi = idx + 8
        if hi > self.size - 1:
            hi = self.size - 1
            lo = max(hi - 16, 0)
        return lo, hi

    def eq(self, i, name):
        return self.k[i] not in (None, '') and h16(self.k[i]) == h16(name) and self.k[i].lower() == name.lower()

    def find(self, name):
        idx = h16(name) & (self.size - 1)
        if self.k[idx] is None:
            return -1
        if self.eq(idx, name):
            return idx
        lo, hi = self.win(idx)
        for p in range(idx + 1, hi + 1):
            if self.k[p] is None:
                return -1
            if self.eq(p, name):
                return p
        for p in range(idx - 1, lo - 1, -1):
            if self.k[p] is None:
                return -1
            if self.eq(p, name):
                return p
        return -1

    def set(self, name, val):
        while True:
            tomb = -1
            idx = h16(name) & (self.size - 1)
            if self.k[idx] is None:
                self.k[idx] = name
                self.v[idx] = val
                return
            if self.k[idx] == '':
                tomb = idx
            elif self.eq(idx, name):
                self.v[idx] = val
                return
            lo, hi = self.win(idx)
            order = list(range(idx + 1, hi + 1)) + list(range(idx - 1, lo - 1, -1))
            for p in order:
                if self.k[p] is None:
                    self.k[p] = name
                    self.v[p] = val
                    return
                if self.k[p] == '':
                    if tomb != -1:
                        tomb = p
                elif self.eq(p, name):
                    self.v[p] = val
                    return
            if tomb == -1:
                old = [(k, v) for k, v in zip(self.k, self.v) if k]
                self.size *= 2
                self.k = [None] * self.size
                self.v = [None] * self.size
                for k, v in old:
                    self.set(k, v)
                continue
            self.k[tomb] = name
            self.v[tomb] = val
            return

    def remove(self, name):
        i = self.find(name)
        if i >= 0:
            self.k[i] = ''
            self.v[i] = None

    def names(self):
        return [k for k in self.k if k]


def slots(names, size=8):
    t = Tab(size)
    for n in names:
        t.set(n, 1)
    return t


# Scenarios pasted into engine/tests/test_apt_value.cpp (run: python apt_hash_model.py)

A = slots(['one', 'two', 'three', 'four', 'five'])
print('A', A.size, A.names(), [A.find(n) for n in ['one', 'two', 'three', 'four', 'five']])
P = slots(['shared', 'own'])
print('P', P.names())

B = slots(['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'a', 'b', 'c'])
print('B', B.size, B.names(), [(n, B.find(n)) for n in B.names()])

B2 = slots(['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'a', 'b', 'c', 'x', 'y', 'z', 'name', 'value', 'shared', 'own', 'alpha', 'beta', 'gamma', 'delta'])
print('B2', B2.size, B2.names())

# tombstones: insert, delete, reinsert
C = Tab(8)
for n in ['one', 'two', 'three', 'four', 'five']:
    C.set(n, 1)
C.remove('four')
print('C after remove four', C.names(), C.k)
C.set('b', 1)   # home slot 5: slot 5 tomb -> remembered; scan forward slot 6 'five' used, 7 'one' used, backward 4..0: 4 empty -> inserted there
print('C after set b', C.names(), C.k)
C.set('four', 1)
print('C after set four', C.names(), C.k)

# tomb at home reused when window full of used slots
D = Tab(8)
names = ['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'a']
for n in names:
    D.set(n, 1)
print('D', D.size, D.k)
D.remove('one')
D.set('x', 1)   # home 7: tomb at home, scan finds no empty ... table full except tomb -> reuse
print('D after', D.size, D.k)

import os.path

class Memory:
    def __init__(self):
        self.ranges = []

class MemoryRange:
    read = False
    write = False
    execute = False
    shared = False
    start = 0
    size = 0
    buffer = bytes()

    @property
    def bad(self):
        return self.size == 0

    @property
    def end(self):
        # Contained in range, unlike t_memrange::end
        return self.start + self.size - 1
    @end.setter
    def end(self, value):
        if value < self.start:
            raise ValueError
        self.size = value - self.start + 1

    @property
    def short_name(self):
        return os.path.basename(self.name)

    def __repr__(self):
        return '%s(%r, 0x%x-0x%x, %s%s%s%s)' % (
            self.__class__.__name__,
            self.short_name,
            self.start,
            self.end,
            'r' if self.read else '-',
            'w' if self.write else '-',
            'x' if self.execute else '-',
            's' if self.shared else '-',
        )

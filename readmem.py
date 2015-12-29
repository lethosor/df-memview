import hashlib, socket, struct, sys, time

from memory import Memory, MemoryRange

def read(sock, length, timeout=1):
    end_time = time.time() + timeout
    data = b''
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            if time.time() > end_time:
                raise EOFError('only got %i/%i bytes' % (len(data), length))
            time.sleep(0.01)
        else:
            end_time = time.time() + timeout
        data += chunk
    return data

def read_int_wrapper(format_char, size):
    def inner(sock):
        return struct.unpack('<' + format_char, read(sock, size))[0]
    return inner

read_uint8 = read_int_wrapper('B', 1)
read_uint32 = read_int_wrapper('I', 4)
read_uint64 = read_int_wrapper('Q', 8)

def read_from_port(port=5020, process=lambda data: None):
    s = socket.socket()
    s.connect(('localhost', 5020))
    s.setblocking(1)
    print('process id: %i' % read_uint32(s))
    arch = read_uint8(s)
    read_ptr = {32: read_uint32, 64: read_uint64}[arch]
    num_ranges = read_uint32(s)
    memory = Memory()
    try:
        for i in range(num_ranges):
            mrange = MemoryRange()
            mrange.name = read(s, read_uint32(s))
            for flag in ('read', 'write', 'execute', 'shared'):
                if read_uint8(s):
                    setattr(mrange, flag, True)
            mrange.start = read_ptr(s)
            mrange.size = read_uint32(s)
            if not mrange.bad:
                mrange.buffer = read(s, mrange.size)
            name_md5 = read(s, 32)
            assert(name_md5 == hashlib.md5(mrange.name).hexdigest().encode('utf-8'))
            if not mrange.bad:
                try:
                    process({'index': i, 'count': i+1, 'total': num_ranges, 'range': mrange})
                except Exception:
                    pass
                memory.ranges.append(mrange)
        assert(s.recv(1) == b'')
    except Exception:
        import traceback
        traceback.print_exc()
    finally:
        # s.shutdown(socket.SHUT_RDWR)
        s.close()
    return memory

if __name__ == '__main__':
    def _process(data):
        print('{count}/{total}: {range!r}'.format(**data))
    start = time.time()
    memory = read_from_port(process=_process)
    print('%i bytes' % sum(map(lambda r: r.size if r.read else 0, memory.ranges)))
    print('read in %f seconds' % (time.time() - start))
    sys.stdin.readline()
    print('')

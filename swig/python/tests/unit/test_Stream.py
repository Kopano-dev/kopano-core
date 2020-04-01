from MAPI import (IStream, BOOKMARK_BEGINNING, BOOKMARK_CURRENT, BOOKMARK_END)


def test_write(stream):
    data = b"test"
    stream.Write(data)
    stat = stream.Stat(0)
    assert stat.cbSize == 4


def test_create_truncate(stream):
    data = b'hello'
    stream.Write(data)
    stream.Seek(0, 0)
    stream.Write(data)
    stream.Seek(0, 0)
    assert stream.Read(15) == data


def test_create_append(stream):
    data = b'hello'
    stream.Write(data)
    stream.Seek(0, BOOKMARK_END)
    stream.Write(data)
    stream.Seek(0, 0)
    assert stream.Read(15) == data + data


def test_commit(stream):
    data = b"hello"
    stream.Write(data)
    assert stream.Read(10) == b''
    stream.Commit(0)

    stream.Seek(0, 0)
    stream.Read(10) == data
    stream.Write(data)
    stream.Read(10) == b''
    stream.Commit(0)
    stream.Read(10) == b''

    stream.Seek(0, 0)
    stream.Read(15) == data+data

    stream.Seek(0, 0)
    stream.Write(b'aaaaa')
    stream.Revert()
    stream.Write(b'bb')
    stream.Commit(0)

    stream.Seek(0, 0)
    stream.Read(10) == b'bbllohallo'


def test_revert(stream):
    stream.Write(b'a')
    stream.Commit(0)

    stream.Seek(0, 0)
    stream.Write(b'b')
    stream.Revert()

    stream.Seek(0, 0)
    stream.Write(b'c')
    stream.Commit(0)
    stream.Seek(0, 0)
    assert stream.Read(15) == b'c'


def test_seek_current(stream):
    stream.Seek(2, BOOKMARK_CURRENT)
    stream.Write(b'aa')
    stream.Seek(-2, BOOKMARK_CURRENT)
    stream.Write(b'b')
    stream.Seek(0, 0)
    assert stream.Read(15) == b'ba'


def test_seek_beginning(stream):
    stream.Seek(2, BOOKMARK_BEGINNING)
    stream.Write(b'aa')
    stream.Seek(1, BOOKMARK_BEGINNING)
    stream.Write(b'b')
    stream.Seek(0, BOOKMARK_BEGINNING)
    assert stream.Read(2) == b'ab'


def test_copyto(stream):
    stream.Write(b'Hello')
    stream.Seek(2, BOOKMARK_BEGINNING)

    stream2 = IStream()
    res = stream.CopyTo(stream2, 2)
    assert res == (2, 2)

    stream2.Seek(0, BOOKMARK_BEGINNING)
    assert stream2.Read(3) == b'll'
    assert stream.Seek(0, BOOKMARK_CURRENT) == 4

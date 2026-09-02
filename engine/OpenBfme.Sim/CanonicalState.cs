using System.Security.Cryptography;
using System.Text;

namespace OpenBfme.Sim;

/// <summary>
/// Canonical little-endian binary writer. Every piece of authoritative state is
/// serialized through this type, so the state hash and the snapshot format are
/// the same bytes by construction.
/// </summary>
public sealed class CanonicalWriter
{
    private readonly MemoryStream _stream = new();
    private readonly BinaryWriter _writer;

    public CanonicalWriter()
    {
        _writer = new BinaryWriter(_stream, Encoding.UTF8, leaveOpen: true);
    }

    public void WriteByte(byte value) => _writer.Write(value);
    public void WriteInt(int value) => _writer.Write(value);
    public void WriteLong(long value) => _writer.Write(value);
    public void WriteBool(bool value) => _writer.Write(value ? (byte)1 : (byte)0);
    public void WriteFixed(Fixed64 value) => _writer.Write(value.Raw);

    public void WriteVector(FixedVector2 value)
    {
        _writer.Write(value.X.Raw);
        _writer.Write(value.Y.Raw);
    }

    public void WriteString(string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        _writer.Write(bytes.Length);
        _writer.Write(bytes);
    }

    public byte[] ToArray()
    {
        _writer.Flush();
        return _stream.ToArray();
    }

    public string ToSha256Hex()
    {
        _writer.Flush();
        return Convert.ToHexString(SHA256.HashData(_stream.ToArray())).ToLowerInvariant();
    }
}

public sealed class CanonicalReader
{
    private readonly BinaryReader _reader;

    public CanonicalReader(byte[] payload)
    {
        _reader = new BinaryReader(new MemoryStream(payload, writable: false), Encoding.UTF8);
    }

    public byte ReadByte() => _reader.ReadByte();
    public int ReadInt() => _reader.ReadInt32();
    public long ReadLong() => _reader.ReadInt64();
    public bool ReadBool() => _reader.ReadByte() != 0;
    public Fixed64 ReadFixed() => Fixed64.FromRaw(_reader.ReadInt64());
    public FixedVector2 ReadVector() => new(Fixed64.FromRaw(_reader.ReadInt64()), Fixed64.FromRaw(_reader.ReadInt64()));
    public bool HasRemaining => _reader.BaseStream.Position < _reader.BaseStream.Length;

    public string ReadString()
    {
        var length = _reader.ReadInt32();
        if (length < 0)
        {
            throw new InvalidDataException("Negative string length in canonical payload");
        }
        return Encoding.UTF8.GetString(_reader.ReadBytes(length));
    }

    public void ExpectEnd()
    {
        if (_reader.BaseStream.Position != _reader.BaseStream.Length)
        {
            throw new InvalidDataException(
                $"Canonical payload has {_reader.BaseStream.Length - _reader.BaseStream.Position} trailing bytes");
        }
    }
}

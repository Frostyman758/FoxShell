using System;
using System.IO;
using MgsvModBldr.Tools.Ftex.Exceptions;

namespace MgsvModBldr.Tools.Ftex
{
    // AOT-safe drop-in replacement for the Ftex tool's ExtensionMethods. The
    // original reads values via Marshal.SizeOf + Marshal.PtrToStructure, which
    // NativeAOT cannot do (no struct marshalling metadata). The Assert calls in
    // the Ftex readers only ever use primitive types, so we read those directly.
    // Same namespace + class name so the Ftex sources bind to this instead.
    internal static class ExtensionMethods
    {
        internal static void Skip(this BinaryReader reader, int count)
            => reader.BaseStream.Seek(count, SeekOrigin.Current);

        internal static void WriteZeros(this BinaryWriter writer, int count)
            => writer.Write(new byte[count]);

        internal static void Assert<T>(this BinaryReader reader, T expected, string message = "") where T : struct
        {
            object actual;
            var t = typeof(T);
            if (t == typeof(long))        actual = reader.ReadInt64();
            else if (t == typeof(ulong))  actual = reader.ReadUInt64();
            else if (t == typeof(int))    actual = reader.ReadInt32();
            else if (t == typeof(uint))   actual = reader.ReadUInt32();
            else if (t == typeof(short))  actual = reader.ReadInt16();
            else if (t == typeof(ushort)) actual = reader.ReadUInt16();
            else if (t == typeof(byte))   actual = reader.ReadByte();
            else if (t == typeof(sbyte))  actual = reader.ReadSByte();
            else throw new NotSupportedException($"Assert<{t.Name}> not supported (AOT shim).");

            if (!actual.Equals(expected))
                throw new AssertionFailedException(message);
        }

        internal static int Align(this BinaryWriter writer, int alignment)
        {
            int rem = (int)(writer.BaseStream.Position % alignment);
            if (rem > 0) { int add = alignment - rem; writer.BaseStream.Position += add; return add; }
            return 0;
        }

        internal static int SizeOf(this Type type)
        {
            if (type == typeof(long) || type == typeof(ulong)) return 8;
            if (type == typeof(int) || type == typeof(uint)) return 4;
            if (type == typeof(short) || type == typeof(ushort)) return 2;
            if (type == typeof(byte) || type == typeof(sbyte)) return 1;
            throw new NotSupportedException($"SizeOf({type.Name}) not supported (AOT shim).");
        }
    }
}

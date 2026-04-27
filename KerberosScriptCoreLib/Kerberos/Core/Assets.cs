namespace Kerberos.Source.Kerberos.Core;

using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public readonly struct MaterialRef { public readonly ulong Handle; }

[StructLayout(LayoutKind.Sequential)]
public readonly struct MeshRef { public readonly ulong Handle; }

[StructLayout(LayoutKind.Sequential)]
public readonly struct TextureRef { public readonly ulong Handle; }
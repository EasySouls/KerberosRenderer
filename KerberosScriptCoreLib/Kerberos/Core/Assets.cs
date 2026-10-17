namespace Kerberos.Source.Kerberos.Core;

using System.Runtime.InteropServices;

/// <summary>
/// Reference to a material asset.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct MaterialRef { public readonly ulong Handle; }

/// <summary>
/// Reference to a mesh asset.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct MeshRef { public readonly ulong Handle; }

/// <summary>
/// Reference to a texture asset.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct TextureRef { public readonly ulong Handle; }

/// <summary>
/// Reference to a prefab asset. Assign one from the editor and pass it to
/// Entity.Instantiate(PrefabRef) to spawn the prefab at runtime.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct PrefabRef { public readonly ulong Handle; }

/// <summary>
/// Untyped reference to any asset registered in the project.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct AssetHandle { public readonly ulong Handle; }
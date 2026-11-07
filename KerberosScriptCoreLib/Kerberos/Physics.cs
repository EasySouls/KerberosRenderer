using System;
using System.Runtime.InteropServices;
using Kerberos.Core;
using Kerberos.Scene;

namespace Kerberos;

public enum BodyType
{
    Static,
    Dynamic,
    Kinematic
}

public struct RaycastHit
{
    public Vector3 Point;
    public Vector3 Normal;
    public Entity Entity;
}

public struct Collision
{
    public Entity Entity;
    public Vector3 ContactPoint;
    public Vector3 WorldNormal;
    public float PenetrationDepth;
}

public static class Physics
{
    /// <summary>
    /// Maximum number of unique entities returned by a single overlap query.
    /// </summary>
    public const int MaxOverlapResults = 64;

    private static Entity[] ReadOverlapEntities(IntPtr buffer, int count)
    {
        if (count <= 0)
            return Array.Empty<Entity>();

        long[] ids = new long[count];
        Marshal.Copy(buffer, ids, 0, count);

        Entity[] entities = new Entity[count];
        for (int i = 0; i < count; ++i)
            entities[i] = new Entity((ulong)ids[i]);
        return entities;
    }

    /// <summary>
    /// Returns entities overlapping a world-space, axis-aligned box.
    /// </summary>
    public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents)
    {
        IntPtr buffer = Marshal.AllocHGlobal(MaxOverlapResults * sizeof(long));
        try
        {
            int count = InternalCalls.Physics_OverlapBox(
                ref center, ref halfExtents, buffer, MaxOverlapResults);
            return ReadOverlapEntities(buffer, count);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>
    /// Returns entities overlapping a box defined in the source entity's local space. This is the
    /// intended hitbox query: the source entity can be a socket or weapon whose world transform is
    /// driven by animation.
    /// </summary>
    public static Entity[] OverlapBox(Entity source, Vector3 localCenter, Vector3 localHalfExtents,
                                      Entity ignore = null)
    {
        if (source == null)
            return Array.Empty<Entity>();

        ulong ignoreEntityID = ignore != null ? ignore.GetID() : 0;

        IntPtr buffer = Marshal.AllocHGlobal(MaxOverlapResults * sizeof(long));
        try
        {
            int count = InternalCalls.Physics_OverlapBoxForEntity(
                source.GetID(), ref localCenter, ref localHalfExtents, ignoreEntityID,
                buffer, MaxOverlapResults);
            return ReadOverlapEntities(buffer, count);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>
    /// Returns entities overlapping a world-space sphere.
    /// </summary>
    public static Entity[] OverlapSphere(Vector3 center, float radius)
    {
        IntPtr buffer = Marshal.AllocHGlobal(MaxOverlapResults * sizeof(long));
        try
        {
            int count = InternalCalls.Physics_OverlapSphere(
                ref center, radius, buffer, MaxOverlapResults);
            return ReadOverlapEntities(buffer, count);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>
    /// Returns entities overlapping a sphere defined in the source entity's local space.
    /// </summary>
    public static Entity[] OverlapSphere(Entity source, Vector3 localCenter, float radius,
                                         Entity ignore = null)
    {
        if (source == null)
            return Array.Empty<Entity>();

        ulong ignoreEntityID = ignore != null ? ignore.GetID() : 0;

        IntPtr buffer = Marshal.AllocHGlobal(MaxOverlapResults * sizeof(long));
        try
        {
            int count = InternalCalls.Physics_OverlapSphereForEntity(
                source.GetID(), ref localCenter, radius, ignoreEntityID,
                buffer, MaxOverlapResults);
            return ReadOverlapEntities(buffer, count);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>
    /// Performs a world-space raycast.
    /// The direction is interpreted in world space and is normalized by the native physics backend.
    /// </summary>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hitInfo)
    {
        hitInfo = new RaycastHit();
        Vector3 hitPoint = Vector3.Zero;
        Vector3 hitNormal = Vector3.Zero;
        ulong entityID = 0;

        if (InternalCalls.Physics_Raycast(ref origin, ref direction, maxDistance, out hitPoint, out hitNormal, out entityID))
        {
            hitInfo = new RaycastHit
            {
                Point = hitPoint,
                Normal = hitNormal,
                Entity = new Entity(entityID)
            };
            return true;
        }

        return false;
    }
}
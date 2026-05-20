using System;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source.Kerberos;

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
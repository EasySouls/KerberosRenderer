using System;
using System.IO;

namespace Kerberos.Source.Kerberos.Core
{
    public static class Constants
    {
        public const double Rad2Deg = 360 / (Math.PI * 2);
        public const double Deg2Rad = (Math.PI * 2) / 360;
    }


    public static class Maths
    {
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
        {
            if (t <= 0) return a;
            if (t >= 1) return b;
            return a + (b - a) * t;
        }
    }


    public struct Vector2
    {
        public float X;
        public float Y;

        public Vector2(float x, float y)
        {
            X = x;
            Y = y;
        }

        public Vector2(float scalar)
        {
            X = scalar;
            Y = scalar;
        }

        public float Magnitude => (float)Math.Sqrt(X * X + Y * Y);
        public float SqrMagnitude => X * X + Y * Y;

        public static Vector2 Normalize(Vector2 vector)
        {
            float magnitude = vector.Magnitude;
            if (magnitude == 0) return Vector2.Zero;
            return vector / magnitude;
        }

        public void Normalize()
        {
            this = Normalize(this);
        }

        public Vector2 Normalized()
        {
            return Normalize(this);
        }

        public static Vector2 Cross(Vector2 a, Vector2 b)
        {
            return new Vector2(
                a.X * b.Y - a.Y * b.X
            );
        }

        public Vector2 Cross(ref Vector2 other)
        {
            return Cross(this, other);
        }

        public static float Dot(Vector2 a, Vector2 b)
        {
            return a.X * b.X + a.Y * b.Y;
        }

        public float Dot(ref Vector2 other)
        {
            return Dot(this, other);
        }

        public static float Angle(Vector2 from, Vector2 to)
        {
            float dot = Dot(from.Normalized(), to.Normalized());
            return (float)(Math.Acos(dot) * Constants.Rad2Deg);
        }

        public float Angle(ref Vector2 other)
        {
            return Angle(this, other);
        }

        public static float Distance(Vector2 a, Vector2 b)
        {
            return (a - b).Magnitude;
        }

        public float Distance(ref Vector2 other)
        {
            return Distance(this, other);
        }

        public static Vector2 Zero => new Vector2(0.0f);

        public static Vector2 operator +(Vector2 a, Vector2 b)
        {
            return new Vector2(a.X + b.X, a.Y + b.Y);
        }
        public static Vector2 operator -(Vector2 a, Vector2 b)
        {
            return new Vector2(a.X - b.X, a.Y - b.Y);
        }
        public static Vector2 operator *(Vector2 a, float scalar)
        {
            return new Vector2(a.X * scalar, a.Y * scalar);
        }
        public static Vector2 operator /(Vector2 a, float scalar)
        {
            return new Vector2(a.X / scalar, a.Y / scalar);
        }
        public override string ToString()
        {
            return $"({X}, {Y})";
        }
    }

    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public Vector3(float scalar)
        {
            X = scalar;
            Y = scalar;
            Z = scalar;
        }

        public float Magnitude => (float)Math.Sqrt(X * X + Y * Y + Z * Z);
        public float SqrMagnitude => X * X + Y * Y + Z * Z;

        public static Vector3 Normalize(Vector3 vector)
        {
            float magnitude = vector.Magnitude;
            if (magnitude == 0) return Vector3.Zero;
            return vector / magnitude;
        }

        public void Normalize()
        {
            this = Normalize(this);
        }

        public Vector3 Normalized()
        {
            return Normalize(this);
        }

        public static Vector3 Cross(Vector3 a, Vector3 b)
        {
            return new Vector3(
                a.Y * b.Z - a.Z * b.Y,
                a.Z * b.X - a.X * b.Z,
                a.X * b.Y - a.Y * b.X
            );
        }

        public void Cross(ref Vector3 other)
        {
            this = Cross(this, other);
        }

        public static float Dot(Vector3 a, Vector3 b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        public float Dot(ref Vector3 other)
        {
            return Dot(this, other);
        }

        public static float Angle(Vector3 from, Vector3 to)
        {
            float dot = Dot(from.Normalized(), to.Normalized());
            return (float)(Math.Acos(dot) * Constants.Rad2Deg);
        }

        public float Angle(ref Vector3 other)
        {
            return Angle(this, other);
        }

        public static float Distance(Vector3 a, Vector3 b)
        {
            return (a - b).Magnitude;
        }

        public float Distance(ref Vector3 other)
        {
            return Distance(this, other);
        }

        public static Vector3 Zero => new Vector3(0.0f);
        public static Vector3 Identity => new Vector3(1.0f);
        public static Vector3 Up => new Vector3(0.0f, 1.0f, 0.0f);
        public static Vector3 Down => new Vector3(0.0f, -1.0f, 0.0f);
        public static Vector3 Forward => new Vector3(0.0f, 0.0f, 1.0f);
        public static Vector3 Back => new Vector3(0.0f, 0.0f, -1.0f);
        public static Vector3 Right => new Vector3(1.0f, 0.0f, 0.0f);
        public static Vector3 Left => new Vector3(-1.0f, 0.0f, 0.0f);


        public static Vector3 operator +(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }
        public static Vector3 operator -(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }
        public static Vector3 operator *(Vector3 a, float scalar)
        {
            return new Vector3(a.X * scalar, a.Y * scalar, a.Z * scalar);
        }
        public static Vector3 operator /(Vector3 a, float scalar)
        {
            return new Vector3(a.X / scalar, a.Y / scalar, a.Z / scalar);
        }
        public override string ToString()
        {
            return $"({X}, {Y}, {Z})";
        }
    }

    public struct Vector4
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public Vector4(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }
        public Vector4(float scalar)
        {
            X = scalar;
            Y = scalar;
            Z = scalar;
            W = scalar;
        }
        public float Magnitude => (float)Math.Sqrt(X * X + Y * Y + Z * Z + W * W);
        public float SqrMagnitude => X * X + Y * Y + Z * Z + W * W;

        public static Vector4 Normalize(Vector4 vector)
        {
            float magnitude = vector.Magnitude;
            if (magnitude == 0) return Vector4.Zero;
            return vector / magnitude;
        }

        public void Normalize()
        {
            this = Normalize(this);
        }

        public Vector4 Normalized()
        {
            return Normalize(this);
        }

        public static Vector4 Zero => new Vector4(0.0f);
        public static Vector4 Identity => new Vector4(1.0f);

        public static Vector4 operator +(Vector4 a, Vector4 b)
        {
            return new Vector4(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
        }
        public static Vector4 operator -(Vector4 a, Vector4 b)
        {
            return new Vector4(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
        }
        public static Vector4 operator *(Vector4 a, float scalar)
        {
            return new Vector4(a.X * scalar, a.Y * scalar, a.Z * scalar, a.W * scalar);
        }
        public static Vector4 operator /(Vector4 a, float scalar)
        {
            return new Vector4(a.X / scalar, a.Y / scalar, a.Z / scalar, a.W / scalar);
        }
        public override string ToString()
        {
            return $"({X}, {Y}, {Z}, {W})";
        }
    }
}

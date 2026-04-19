using System;
using System.IO;

namespace Kerberos.Source.Kerberos.Core
{
    public static class Constants
    {
        public const double Rad2Deg = 360 / (Math.PI * 2);
        public const double Deg2Rad = (Math.PI * 2) / 360;
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

        //public static Vector3 Normalize(Vector3 vector)
        //{

        //}

        //public void Normalize()
        //{
        //    this = Normalize(this);
        //}

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

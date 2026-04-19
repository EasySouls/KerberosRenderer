using Kerberos.Source.Kerberos.Core;
using System;
using System.IO;

namespace Kerberos.Source.Kerberos.Scene
{
    public abstract class Component
    {
        public Entity Entity { get; internal set; }
    }

    public class TagComponent : Component
    {
        public string Tag { get; set; }
    }

    public class TransformComponent : Component
    {
        public Vector3 Translation
        {
            get
            {
                InternalCalls.TransformComponent_GetTranslation(Entity.GetID(), out Vector3 translation);
                return translation;
            }
            set => InternalCalls.TransformComponent_SetTranslation(Entity.GetID(), ref value);
        }

        public Vector3 Rotation
        {
            get
            {
                InternalCalls.TransformComponent_GetRotation(Entity.GetID(), out Vector3 rotation);
                return rotation;
            }
            set => InternalCalls.TransformComponent_SetRotation(Entity.GetID(), ref value);
        }

        public Vector3 Scale
        {
            get
            {
                InternalCalls.TransformComponent_GetScale(Entity.GetID(), out Vector3 scale);
                return scale;
            }
            set => InternalCalls.TransformComponent_SetScale(Entity.GetID(), ref value);
        }

        public void LookAt(Vector3 targetPosition)
        {
            Vector3 direction = targetPosition - Translation;

            const float epsilon = 0.000000f;
            if (direction.Magnitude < epsilon)
            {
                Rotation = Vector3.Zero;
            }

            double yaw = Math.Atan2(direction.X, direction.Y) * Constants.Rad2Deg;
            float horizontalDistance = new Vector2(direction.X, direction.Z).Magnitude;
            double pitch = -Math.Atan2(direction.Y, horizontalDistance) * Constants.Rad2Deg;
            const double roll = 0;

            Rotation = new Vector3((float)pitch, (float)yaw, (float)roll);
        }
    }

    public class RigidBody3DComponent : Component
    {
        public void ApplyImpulse(Vector3 impulse) => InternalCalls.Rigidbody3DComponent_ApplyImpulse(Entity.GetID(), ref impulse);

        public void ApplyImpulse(Vector3 impulse, Vector3 point) => InternalCalls.Rigidbody3DComponent_ApplyImpulseAtPoint(Entity.GetID(), ref impulse, ref point);
    }

    public class TextComponent : Component
    {
        public string Text
        {
            get => InternalCalls.TextComponent_GetText(Entity.GetID());
            set => InternalCalls.TextComponent_SetText(Entity.GetID(), value);
        }

        public Vector4 Color
        {
            get
            {
                InternalCalls.TextComponent_GetColor(Entity.GetID(), out Vector4 color);
                return color;
            }
            set => InternalCalls.TextComponent_SetColor(Entity.GetID(), ref value);
        }

        public float FontSize
        {
            get => InternalCalls.TextComponent_GetFontSize(Entity.GetID());
            set
            {
                if (value <= 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Font size must be greater than zero.");
                InternalCalls.TextComponent_SetFontSize(Entity.GetID(), value);
            }
        }

        public string FontPath
        {
            get => InternalCalls.TextComponent_GetFontPath(Entity.GetID());
            set
            {
                if (!File.Exists(value))
                    throw new FileNotFoundException($"Font file not found at path: {value}");
                InternalCalls.TextComponent_SetFontPath(Entity.GetID(), value);
            }
        }
    }

    public class AudioSource2DComponent : Component
    {
        public void Play() => InternalCalls.AudioSource2DComponent_Play(Entity.GetID());

        public void Stop() => InternalCalls.AudioSource2DComponent_Stop(Entity.GetID());
    }

    public class AudioSource3DComponent : Component
    {
        public void Play() => InternalCalls.AudioSource3DComponent_Play(Entity.GetID());

        public void Stop() => InternalCalls.AudioSource3DComponent_Stop(Entity.GetID());
    }

    public class AudioListenerComponent : Component
    {
    }
}

using Kerberos.Core;
using System;
using System.IO;

namespace Kerberos.Scene
{
    public abstract class Component()
    {
        public Entity Entity { get; internal set; } = null!;
    }

    public class TagComponent : Component
    {
        public required string Tag { get; set; }
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
            direction.Normalize();

            const float epsilon = 1e-6f;
            if (direction.Magnitude < epsilon)
                return;

            //double yaw = Math.Atan2(direction.X, direction.Y) * Constants.Rad2Deg;
            //float horizontalDistance = new Vector2(direction.X, direction.Z).Magnitude;
            //double pitch = -Math.Atan2(direction.Y, horizontalDistance) * Constants.Rad2Deg;
            //const double roll = 0;

            //Rotation = new Vector3((float)pitch, (float)yaw, (float)roll);

            float horiz = (float)Math.Sqrt(direction.X * direction.X + direction.Z * direction.Z);
            double yaw = Math.Atan2(direction.X, direction.Z) * Constants.Rad2Deg;           // atan2(x, z)
            double pitch = -Math.Atan2(direction.Y, horiz) * Constants.Rad2Deg;        // -atan2(y, horiz)
            Rotation = new Vector3((float)pitch, (float)yaw, 0f);
        }
    }

    public class SocketComponent : Component
    {
    }

    public class AnimatorComponent : Component
    {
        public float CurrentTime
        {
            get => InternalCalls.AnimatorComponent_GetTime(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetTime(Entity.GetID(), value);
        }

        public float PlaybackSpeed
        {
            get => InternalCalls.AnimatorComponent_GetSpeed(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetSpeed(Entity.GetID(), value);
        }

        public bool IsPlaying
        {
            get => InternalCalls.AnimatorComponent_IsPlaying(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetPlaying(Entity.GetID(), value);
        }

        public bool IsLooping
        {
            get => InternalCalls.AnimatorComponent_IsLooping(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetLooping(Entity.GetID(), value);
        }

        public bool PlayOnStart
        {
            get => InternalCalls.AnimatorComponent_GetPlayOnStart(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetPlayOnStart(Entity.GetID(), value);
        }

        public int ActiveClip
        {
            get => InternalCalls.AnimatorComponent_GetActiveClip(Entity.GetID());
            set => InternalCalls.AnimatorComponent_SetActiveClip(Entity.GetID(), value);
        }

        public int ClipCount => InternalCalls.AnimatorComponent_GetClipCount(Entity.GetID());

        /// <summary>
        /// Resumes the current clip, or starts the first clip when none is selected.
        /// </summary>
        public void Play() => InternalCalls.AnimatorComponent_Play(Entity.GetID(), -1);

        /// <summary>
        /// Starts the clip at the given index from the beginning.
        /// </summary>
        public void Play(int clipIndex) => InternalCalls.AnimatorComponent_Play(Entity.GetID(), clipIndex);

        /// <summary>
        /// Starts the first clip whose asset name matches.
        /// </summary>
        public void Play(string clipName) => InternalCalls.AnimatorComponent_PlayByName(Entity.GetID(), clipName);

        /// <summary>
        /// Pauses playback and keeps the current time.
        /// </summary>
        public void Pause() => InternalCalls.AnimatorComponent_Pause(Entity.GetID());

        /// <summary>
        /// Stops playback and resets the current time to zero.
        /// </summary>
        public void Stop() => InternalCalls.AnimatorComponent_Stop(Entity.GetID());

        /// <summary>
        /// Returns the duration of the clip at the given index.
        /// </summary>
        public float GetClipDuration(int clipIndex) => InternalCalls.AnimatorComponent_GetClipDuration(Entity.GetID(), clipIndex);
    }

    public class StaticMeshComponent : Component
    {
        public MeshRef Mesh
        {
            get
            {
                InternalCalls.StaticMeshComponent_GetMesh(Entity.GetID(), out MeshRef mesh);
                return mesh;
            }
            set => InternalCalls.StaticMeshComponent_SetMesh(Entity.GetID(), value);
        }
    }

    public class RigidBody3DComponent : Component
    {
        public BodyType Type
        {
            get => (BodyType)InternalCalls.Rigidbody3DComponent_GetBodyType(Entity.GetID());
            set => InternalCalls.Rigidbody3DComponent_SetBodyType(Entity.GetID(), (int)value);
        }

        public float Mass
        {
            get => InternalCalls.Rigidbody3DComponent_GetMass(Entity.GetID());
            set
            {
                if (value <= 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Mass must be greater than zero.");
                InternalCalls.Rigidbody3DComponent_SetMass(Entity.GetID(), value);
            }
        }

        public Vector3 Velocity
        {
            get
            {
                InternalCalls.Rigidbody3DComponent_GetVelocity(Entity.GetID(), out Vector3 velocity);
                return velocity;
            }
            set => InternalCalls.Rigidbody3DComponent_SetVelocity(Entity.GetID(), ref value);
        }

        public Vector3 AngularVelocity
        {
            get
            {
                InternalCalls.Rigidbody3DComponent_GetAngularVelocity(Entity.GetID(), out Vector3 velocity);
                return velocity;
            }
            set => InternalCalls.Rigidbody3DComponent_SetAngularVelocity(Entity.GetID(), ref value);
        }

        public bool UseGravity
        {
            get => InternalCalls.Rigidbody3DComponent_GetUseGravity(Entity.GetID());
            set => InternalCalls.Rigidbody3DComponent_SetUseGravity(Entity.GetID(), value);
        }

        public float Friction
        {
            get => InternalCalls.Rigidbody3DComponent_GetFriction(Entity.GetID());
            set
            {
                if (value < 0 || value > 1)
                    throw new ArgumentOutOfRangeException(nameof(value), "Friction must be between 0 and 1.");
                InternalCalls.Rigidbody3DComponent_SetFriction(Entity.GetID(), value);
            }
        }

        public float Restitution
        {
            get => InternalCalls.Rigidbody3DComponent_GetRestitution(Entity.GetID());
            set
            {
                if (value < 0 || value > 1)
                    throw new ArgumentOutOfRangeException(nameof(value), "Restitution must be between 0 and 1.");
                InternalCalls.Rigidbody3DComponent_SetRestitution(Entity.GetID(), value);
            }
        }

        public void ApplyImpulse(Vector3 impulse) => InternalCalls.Rigidbody3DComponent_ApplyImpulse(Entity.GetID(), ref impulse);

        public void ApplyImpulse(Vector3 impulse, Vector3 point) => InternalCalls.Rigidbody3DComponent_ApplyImpulseAtPoint(Entity.GetID(), ref impulse, ref point);
    }

    public class BoxCollider3DComponent : Component
    {
        public Vector3 Size
        {
            get
            {
                InternalCalls.BoxCollider3DComponent_GetSize(Entity.GetID(), out Vector3 size);
                return size;
            }
            set => InternalCalls.BoxCollider3DComponent_SetSize(Entity.GetID(), ref value);
        }

        public Vector3 Offset
        {
            get
            {
                InternalCalls.BoxCollider3DComponent_GetOffset(Entity.GetID(), out Vector3 offset);
                return offset;
            }
            set => InternalCalls.BoxCollider3DComponent_SetOffset(Entity.GetID(), ref value);
        }

        public bool IsTrigger
        {
            get => InternalCalls.BoxCollider3DComponent_GetIsTrigger(Entity.GetID());
            set => InternalCalls.BoxCollider3DComponent_SetIsTrigger(Entity.GetID(), value);
        }

        public bool Enabled
        {
            get => InternalCalls.BoxCollider3DComponent_GetEnabled(Entity.GetID());
            set => InternalCalls.BoxCollider3DComponent_SetEnabled(Entity.GetID(), value);
        }
    }

    public class SphereCollider3DComponent : Component
    {
        public float Radius
        {
            get => InternalCalls.SphereCollider3DComponent_GetRadius(Entity.GetID());
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Radius must not be negative.");
                InternalCalls.SphereCollider3DComponent_SetRadius(Entity.GetID(), value);
            }
        }

        public Vector3 Offset
        {
            get
            {
                InternalCalls.SphereCollider3DComponent_GetOffset(Entity.GetID(), out Vector3 offset);
                return offset;
            }
            set => InternalCalls.SphereCollider3DComponent_SetOffset(Entity.GetID(), ref value);
        }

        public bool IsTrigger
        {
            get => InternalCalls.SphereCollider3DComponent_GetIsTrigger(Entity.GetID());
            set => InternalCalls.SphereCollider3DComponent_SetIsTrigger(Entity.GetID(), value);
        }

        public bool Enabled
        {
            get => InternalCalls.SphereCollider3DComponent_GetEnabled(Entity.GetID());
            set => InternalCalls.SphereCollider3DComponent_SetEnabled(Entity.GetID(), value);
        }
    }

    public class CapsuleCollider3DComponent : Component
    {
        public float Radius
        {
            get => InternalCalls.CapsuleCollider3DComponent_GetRadius(Entity.GetID());
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Radius must not be negative.");
                InternalCalls.CapsuleCollider3DComponent_SetRadius(Entity.GetID(), value);
            }
        }

        public float Height
        {
            get => InternalCalls.CapsuleCollider3DComponent_GetHeight(Entity.GetID());
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Height must not be negative.");
                InternalCalls.CapsuleCollider3DComponent_SetHeight(Entity.GetID(), value);
            }
        }

        public Vector3 Offset
        {
            get
            {
                InternalCalls.CapsuleCollider3DComponent_GetOffset(Entity.GetID(), out Vector3 offset);
                return offset;
            }
            set => InternalCalls.CapsuleCollider3DComponent_SetOffset(Entity.GetID(), ref value);
        }

        public bool IsTrigger
        {
            get => InternalCalls.CapsuleCollider3DComponent_GetIsTrigger(Entity.GetID());
            set => InternalCalls.CapsuleCollider3DComponent_SetIsTrigger(Entity.GetID(), value);
        }

        public bool Enabled
        {
            get => InternalCalls.CapsuleCollider3DComponent_GetEnabled(Entity.GetID());
            set => InternalCalls.CapsuleCollider3DComponent_SetEnabled(Entity.GetID(), value);
        }
    }

    public class MeshCollider3DComponent : Component
    {
        public MeshRef Mesh
        {
            get
            {
                InternalCalls.MeshCollider3DComponent_GetMesh(Entity.GetID(), out MeshRef mesh);
                return mesh;
            }
            set => InternalCalls.MeshCollider3DComponent_SetMesh(Entity.GetID(), value);
        }

        public Vector3 Offset
        {
            get
            {
                InternalCalls.MeshCollider3DComponent_GetOffset(Entity.GetID(), out Vector3 offset);
                return offset;
            }
            set => InternalCalls.MeshCollider3DComponent_SetOffset(Entity.GetID(), ref value);
        }

        public bool IsTrigger
        {
            get => InternalCalls.MeshCollider3DComponent_GetIsTrigger(Entity.GetID());
            set => InternalCalls.MeshCollider3DComponent_SetIsTrigger(Entity.GetID(), value);
        }

        public bool Enabled
        {
            get => InternalCalls.MeshCollider3DComponent_GetEnabled(Entity.GetID());
            set => InternalCalls.MeshCollider3DComponent_SetEnabled(Entity.GetID(), value);
        }
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

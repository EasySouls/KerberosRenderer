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

        /// <summary>
        /// Blends from the currently playing pose into the clip at the given index.
        /// A non-positive duration snaps immediately. Interrupting an in-flight cross-fade
        /// starts from the current blended pose.
        /// </summary>
        public void CrossFade(int clipIndex, float duration, bool restart = true)
            => InternalCalls.AnimatorComponent_CrossFade(Entity.GetID(), clipIndex, duration, restart);

        /// <summary>
        /// Blends from the currently playing pose into the first clip whose asset name matches.
        /// </summary>
        public void CrossFade(string clipName, float duration, bool restart = true)
            => InternalCalls.AnimatorComponent_CrossFadeByName(Entity.GetID(), clipName, duration, restart);
    }

    public enum RagdollState
    {
        Animated = 0,
        BlendingToRagdoll,
        Ragdoll,
        BlendingToAnimation
    }

    public class RagdollComponent : Component
    {
        public float BlendDuration
        {
            get => InternalCalls.RagdollComponent_GetBlendDuration(Entity.GetID());
            set
            {
                if (value < 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Blend duration must not be negative.");
                InternalCalls.RagdollComponent_SetBlendDuration(Entity.GetID(), value);
            }
        }

        public bool StartInRagdoll
        {
            get => InternalCalls.RagdollComponent_GetStartInRagdoll(Entity.GetID());
            set => InternalCalls.RagdollComponent_SetStartInRagdoll(Entity.GetID(), value);
        }

        public float JointRadiusScale
        {
            get => InternalCalls.RagdollComponent_GetJointRadiusScale(Entity.GetID());
            set
            {
                if (value <= 0)
                    throw new ArgumentOutOfRangeException(nameof(value), "Joint radius scale must be positive.");
                InternalCalls.RagdollComponent_SetJointRadiusScale(Entity.GetID(), value);
            }
        }

        public bool IsRagdoll => InternalCalls.RagdollComponent_IsRagdoll(Entity.GetID());

        public RagdollState State => (RagdollState)InternalCalls.RagdollComponent_GetState(Entity.GetID());

        /// <summary>
        /// Physics contribution to the final pose. 0 is fully animated, 1 is fully simulated.
        /// </summary>
        public float BlendWeight => InternalCalls.RagdollComponent_GetWeight(Entity.GetID());

        /// <summary>
        /// Starts blending from the current animation pose into the simulated ragdoll.
        /// </summary>
        public void EnableRagdoll() => InternalCalls.RagdollComponent_EnableRagdoll(Entity.GetID());

        /// <summary>
        /// Blends from the simulated ragdoll back to the current animation pose.
        /// </summary>
        public void DisableRagdoll() => InternalCalls.RagdollComponent_DisableRagdoll(Entity.GetID());
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

    // ----------------------------- AI -----------------------------

    public class AIBlackboardComponent : Component
    {
        public bool Has(string key) => InternalCalls.AIBlackboard_HasKey(Entity.GetID(), key);

        public void Remove(string key) => InternalCalls.AIBlackboard_RemoveKey(Entity.GetID(), key);

        public void Clear() => InternalCalls.AIBlackboard_Clear(Entity.GetID());

        public bool GetBool(string key) => InternalCalls.AIBlackboard_GetBool(Entity.GetID(), key);

        public void SetBool(string key, bool value) => InternalCalls.AIBlackboard_SetBool(Entity.GetID(), key, value);

        public long GetInt(string key) => InternalCalls.AIBlackboard_GetInt(Entity.GetID(), key);

        public void SetInt(string key, long value) => InternalCalls.AIBlackboard_SetInt(Entity.GetID(), key, value);

        public float GetFloat(string key) => InternalCalls.AIBlackboard_GetFloat(Entity.GetID(), key);

        public void SetFloat(string key, float value) => InternalCalls.AIBlackboard_SetFloat(Entity.GetID(), key, value);

        public Vector3 GetVector3(string key)
        {
            InternalCalls.AIBlackboard_GetVec3(Entity.GetID(), key, out Vector3 value);
            return value;
        }

        public void SetVector3(string key, Vector3 value)
            => InternalCalls.AIBlackboard_SetVec3(Entity.GetID(), key, ref value);

        public Entity? GetEntity(string key)
        {
            ulong id = InternalCalls.AIBlackboard_GetEntity(Entity.GetID(), key);
            return id == 0 ? null : new Entity(id);
        }

        public void SetEntity(string key, Entity? value)
            => InternalCalls.AIBlackboard_SetEntity(Entity.GetID(), key, value?.GetID() ?? 0);

        public string GetString(string key) => InternalCalls.AIBlackboard_GetString(Entity.GetID(), key);

        public void SetString(string key, string value)
            => InternalCalls.AIBlackboard_SetString(Entity.GetID(), key, value);
    }

    public class AIStateMachineComponent : Component
    {
        public string TreeName
        {
            get => InternalCalls.AIStateMachine_GetTreeName(Entity.GetID());
            set => InternalCalls.AIStateMachine_SetTreeName(Entity.GetID(), value);
        }

        public string CurrentState => InternalCalls.AIStateMachine_GetCurrentState(Entity.GetID());

        public string PreviousState => InternalCalls.AIStateMachine_GetPreviousState(Entity.GetID());

        public float TimeInState => InternalCalls.AIStateMachine_GetTimeInState(Entity.GetID());

        public bool RequestState(string stateName)
            => InternalCalls.AIStateMachine_RequestState(Entity.GetID(), stateName);

        public void ResetRuntime() => InternalCalls.AIStateMachine_ResetRuntime(Entity.GetID());
    }

    public class AIControllerComponent : Component
    {
        public bool Enabled
        {
            get => InternalCalls.AIController_GetEnabled(Entity.GetID());
            set => InternalCalls.AIController_SetEnabled(Entity.GetID(), value);
        }

        public Vector3 DesiredVelocity
        {
            get
            {
                InternalCalls.AIController_GetDesiredVelocity(Entity.GetID(), out Vector3 value);
                return value;
            }
            set => InternalCalls.AIController_SetDesiredVelocity(Entity.GetID(), ref value);
        }

        public Vector3 LookAtTarget
        {
            get
            {
                InternalCalls.AIController_GetLookAtTarget(Entity.GetID(), out Vector3 value);
                return value;
            }
            set => InternalCalls.AIController_SetLookAtTarget(Entity.GetID(), ref value);
        }

        public bool WantsToAttack
        {
            get => InternalCalls.AIController_GetWantsToAttack(Entity.GetID());
            set => InternalCalls.AIController_SetWantsToAttack(Entity.GetID(), value);
        }
    }

    public class AIPerceptionComponent : Component
    {
        public float SightRadius
        {
            get => InternalCalls.AIPerception_GetSightRadius(Entity.GetID());
            set => InternalCalls.AIPerception_SetSightRadius(Entity.GetID(), value);
        }

        public float LoseRadius
        {
            get => InternalCalls.AIPerception_GetLoseRadius(Entity.GetID());
            set => InternalCalls.AIPerception_SetLoseRadius(Entity.GetID(), value);
        }

        public float FieldOfViewDegrees
        {
            get => InternalCalls.AIPerception_GetFieldOfView(Entity.GetID());
            set => InternalCalls.AIPerception_SetFieldOfView(Entity.GetID(), value);
        }

        public float EyeHeight
        {
            get => InternalCalls.AIPerception_GetEyeHeight(Entity.GetID());
            set => InternalCalls.AIPerception_SetEyeHeight(Entity.GetID(), value);
        }

        public float PerceptionInterval
        {
            get => InternalCalls.AIPerception_GetInterval(Entity.GetID());
            set => InternalCalls.AIPerception_SetInterval(Entity.GetID(), value);
        }

        public bool RequireLineOfSight
        {
            get => InternalCalls.AIPerception_GetRequireLineOfSight(Entity.GetID());
            set => InternalCalls.AIPerception_SetRequireLineOfSight(Entity.GetID(), value);
        }

        public int FactionId
        {
            get => InternalCalls.AIPerception_GetFactionId(Entity.GetID());
            set => InternalCalls.AIPerception_SetFactionId(Entity.GetID(), value);
        }
    }

    public class AITargetableComponent : Component
    {
        public int FactionId
        {
            get => InternalCalls.AITargetable_GetFactionId(Entity.GetID());
            set => InternalCalls.AITargetable_SetFactionId(Entity.GetID(), value);
        }
    }
}

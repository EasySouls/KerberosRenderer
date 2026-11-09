using System;
using System.Runtime.InteropServices;
using Kerberos.Core;
using Kerberos.Scene;

namespace Kerberos
{
    /// <summary>
    /// Provides access to native engine functions via function pointers.
    /// Function pointers are set by the C++ engine during initialization via SetNativeCallbacks.
    /// </summary>
    public static class InternalCalls
    {
        // ====================================================================
        // Delegate types matching native function signatures
        // ====================================================================

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void NativeLogFn(IntPtr message);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte EntityHasComponentFn(ulong entityID, IntPtr componentTypeName);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte EntityAddComponentFn(ulong entityID, IntPtr componentTypeName);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityFindByNameFn(IntPtr name);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityInstantiateFn(IntPtr name);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityInstantiatePrefabFn(ulong prefabHandle);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void GetVec3Fn(ulong entityID, out Vector3 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetVec3Fn(ulong entityID, ref Vector3 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void StaticMeshGetMeshFn(ulong entityID, out MeshRef mesh);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void StaticMeshSetMeshFn(ulong entityID, MeshRef mesh);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void Rigidbody3DApplyImpulseFn(ulong entityID, ref Vector3 impulse);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void Rigidbody3DApplyImpulseAtPointFn(ulong entityID, ref Vector3 impulse, ref Vector3 point);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetStringFn(ulong entityID, IntPtr text);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr GetStringFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetVec4Fn(ulong entityID, ref Vector4 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void GetVec4Fn(ulong entityID, out Vector4 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetFloatFn(ulong entityID, float value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float GetFloatFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte IsKeyDownFn(int keyCode);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte IsMouseButtonDownFn(int mouseCode);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void EntityActionFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetBoolFn(ulong entityID, byte value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte GetBoolFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte PhysicsRaycastFn(ref Vector3 origin, ref Vector3 direction, float maxDistance, out Vector3 hitPoint, out Vector3 hitNormal, out ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PhysicsOverlapBoxFn(ref Vector3 center, ref Vector3 halfExtents, IntPtr outEntities, int maxEntities);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PhysicsOverlapBoxForEntityFn(ulong entityID, ref Vector3 localCenter, ref Vector3 localHalfExtents, ulong ignoreEntityID, IntPtr outEntities, int maxEntities);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PhysicsOverlapSphereFn(ref Vector3 center, float radius, IntPtr outEntities, int maxEntities);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int PhysicsOverlapSphereForEntityFn(ulong entityID, ref Vector3 localCenter, float radius, ulong ignoreEntityID, IntPtr outEntities, int maxEntities);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AnimatorPlayFn(ulong entityID, int clipIndex);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AnimatorPlayByNameFn(ulong entityID, IntPtr clipName);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int GetIntFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SetIntFn(ulong entityID, int value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float AnimatorGetClipDurationFn(ulong entityID, int clipIndex);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AnimatorCrossFadeFn(ulong entityID, int clipIndex, float duration, byte restart);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AnimatorCrossFadeByNameFn(ulong entityID, IntPtr clipName, float duration, byte restart);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void SocketAttachFn(ulong entityID, ulong skeletonEntityID, IntPtr jointName,
                                              ref Vector3 translation, ref Vector3 rotation, ref Vector3 scale);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte AINameAndJsonFn(IntPtr name, IntPtr json);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte AINameFn(IntPtr name);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte AIEntityKeyBoolFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeyActionFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float AIEntityKeyFloatFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetFloatFn(ulong entityID, IntPtr key, float value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate long AIEntityKeyLongFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetLongFn(ulong entityID, IntPtr key, long value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte AIEntityKeyByteFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetByteFn(ulong entityID, IntPtr key, byte value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeyVec3Fn(ulong entityID, IntPtr key, out Vector3 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetVec3Fn(ulong entityID, IntPtr key, ref Vector3 value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong AIEntityKeyEntityFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetEntityFn(ulong entityID, IntPtr key, ulong value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr AIEntityKeyStringFn(ulong entityID, IntPtr key);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AIEntityKeySetStringFn(ulong entityID, IntPtr key, IntPtr value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr AIEntityStringFn(ulong entityID);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void AISetEntityStringFn(ulong entityID, IntPtr value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate byte AIEntityNameFn(ulong entityID, IntPtr name);

        // ====================================================================
        // Stored delegate instances (prevent GC collection)
        // ====================================================================

        private static NativeLogFn? s_NativeLog;
        private static EntityHasComponentFn? s_EntityHasComponent;
        private static EntityAddComponentFn? s_EntityAddComponent;
        private static EntityFindByNameFn? s_EntityFindByName;
        private static EntityInstantiateFn? s_EntityInstantiate;
        private static EntityInstantiatePrefabFn? s_EntityInstantiatePrefab;

        private static GetVec3Fn? s_TransformGetTranslation;
        private static SetVec3Fn? s_TransformSetTranslation;
        private static GetVec3Fn? s_TransformGetRotation;
        private static SetVec3Fn? s_TransformSetRotation;
        private static GetVec3Fn? s_TransformGetScale;
        private static SetVec3Fn? s_TransformSetScale;

        private static StaticMeshGetMeshFn? s_StaticMeshGetMesh;
        private static StaticMeshSetMeshFn? s_StaticMeshSetMesh;

        private static GetVec3Fn? s_RigidbodyGetVelocity;
        private static SetVec3Fn? s_RigidbodySetVelocity;

        private static Rigidbody3DApplyImpulseFn? s_Rigidbody3DApplyImpulse;
        private static Rigidbody3DApplyImpulseAtPointFn? s_Rigidbody3DApplyImpulseAtPoint;

        private static SetStringFn? s_TextComponentSetText;
        private static GetStringFn? s_TextComponentGetText;
        private static SetVec4Fn? s_TextComponentSetColor;
        private static GetVec4Fn? s_TextComponentGetColor;
        private static SetFloatFn? s_TextComponentSetFontSize;
        private static GetFloatFn? s_TextComponentGetFontSize;
        private static SetStringFn? s_TextComponentSetFontPath;
        private static GetStringFn? s_TextComponentGetFontPath;

        private static IsKeyDownFn? s_InputIsKeyDown;
        private static IsMouseButtonDownFn? s_InputIsMouseButtonDown;

        private static EntityActionFn? s_AudioSource2DPlay;
        private static EntityActionFn? s_AudioSource2DStop;
        private static SetFloatFn? s_AudioSource2DSetVolume;
        private static GetFloatFn? s_AudioSource2DGetVolume;
        private static SetBoolFn? s_AudioSource2DSetLooping;
        private static GetBoolFn? s_AudioSource2DIsLooping;

        private static EntityActionFn? s_AudioSource3DPlay;
        private static EntityActionFn? s_AudioSource3DStop;
        private static SetFloatFn? s_AudioSource3DSetVolume;
        private static GetFloatFn? s_AudioSource3DGetVolume;
        private static SetBoolFn? s_AudioSource3DSetLooping;
        private static GetBoolFn? s_AudioSource3DIsLooping;

        private static PhysicsRaycastFn? s_PhysicsRaycast;
        private static PhysicsOverlapBoxFn? s_PhysicsOverlapBox;
        private static PhysicsOverlapBoxForEntityFn? s_PhysicsOverlapBoxForEntity;
        private static PhysicsOverlapSphereFn? s_PhysicsOverlapSphere;
        private static PhysicsOverlapSphereForEntityFn? s_PhysicsOverlapSphereForEntity;

        private static AnimatorPlayFn? s_AnimatorPlay;
        private static AnimatorPlayByNameFn? s_AnimatorPlayByName;
        private static EntityActionFn? s_AnimatorStop;
        private static EntityActionFn? s_AnimatorPause;
        private static GetFloatFn? s_AnimatorGetTime;
        private static SetFloatFn? s_AnimatorSetTime;
        private static GetFloatFn? s_AnimatorGetSpeed;
        private static SetFloatFn? s_AnimatorSetSpeed;
        private static GetBoolFn? s_AnimatorIsPlaying;
        private static SetBoolFn? s_AnimatorSetPlaying;
        private static GetBoolFn? s_AnimatorIsLooping;
        private static SetBoolFn? s_AnimatorSetLooping;
        private static GetBoolFn? s_AnimatorGetPlayOnStart;
        private static SetBoolFn? s_AnimatorSetPlayOnStart;
        private static GetIntFn? s_AnimatorGetActiveClip;
        private static SetIntFn? s_AnimatorSetActiveClip;
        private static GetIntFn? s_AnimatorGetClipCount;
        private static AnimatorGetClipDurationFn? s_AnimatorGetClipDuration;
        private static AnimatorCrossFadeFn? s_AnimatorCrossFade;
        private static AnimatorCrossFadeByNameFn? s_AnimatorCrossFadeByName;

        private static SocketAttachFn? s_SocketAttach;
        private static EntityActionFn? s_SocketDetach;

        private static EntityActionFn? s_RagdollEnable;
        private static EntityActionFn? s_RagdollDisable;
        private static GetBoolFn? s_RagdollIsRagdoll;
        private static GetIntFn? s_RagdollGetState;
        private static GetFloatFn? s_RagdollGetWeight;
        private static GetFloatFn? s_RagdollGetBlendDuration;
        private static SetFloatFn? s_RagdollSetBlendDuration;
        private static GetBoolFn? s_RagdollGetStartInRagdoll;
        private static SetBoolFn? s_RagdollSetStartInRagdoll;
        private static GetFloatFn? s_RagdollGetJointRadiusScale;
        private static SetFloatFn? s_RagdollSetJointRadiusScale;

        private static GetIntFn? s_RigidbodyGetBodyType;
        private static SetIntFn? s_RigidbodySetBodyType;
        private static GetFloatFn? s_RigidbodyGetMass;
        private static SetFloatFn? s_RigidbodySetMass;
        private static GetVec3Fn? s_RigidbodyGetAngularVelocity;
        private static SetVec3Fn? s_RigidbodySetAngularVelocity;
        private static GetBoolFn? s_RigidbodyGetUseGravity;
        private static SetBoolFn? s_RigidbodySetUseGravity;
        private static GetFloatFn? s_RigidbodyGetFriction;
        private static SetFloatFn? s_RigidbodySetFriction;
        private static GetFloatFn? s_RigidbodyGetRestitution;
        private static SetFloatFn? s_RigidbodySetRestitution;

        private static GetVec3Fn? s_BoxColliderGetSize;
        private static SetVec3Fn? s_BoxColliderSetSize;
        private static GetVec3Fn? s_BoxColliderGetOffset;
        private static SetVec3Fn? s_BoxColliderSetOffset;
        private static GetBoolFn? s_BoxColliderGetIsTrigger;
        private static SetBoolFn? s_BoxColliderSetIsTrigger;
        private static GetBoolFn? s_BoxColliderGetEnabled;
        private static SetBoolFn? s_BoxColliderSetEnabled;

        private static GetFloatFn? s_SphereColliderGetRadius;
        private static SetFloatFn? s_SphereColliderSetRadius;
        private static GetVec3Fn? s_SphereColliderGetOffset;
        private static SetVec3Fn? s_SphereColliderSetOffset;
        private static GetBoolFn? s_SphereColliderGetIsTrigger;
        private static SetBoolFn? s_SphereColliderSetIsTrigger;
        private static GetBoolFn? s_SphereColliderGetEnabled;
        private static SetBoolFn? s_SphereColliderSetEnabled;

        private static GetFloatFn? s_CapsuleColliderGetRadius;
        private static SetFloatFn? s_CapsuleColliderSetRadius;
        private static GetFloatFn? s_CapsuleColliderGetHeight;
        private static SetFloatFn? s_CapsuleColliderSetHeight;
        private static GetVec3Fn? s_CapsuleColliderGetOffset;
        private static SetVec3Fn? s_CapsuleColliderSetOffset;
        private static GetBoolFn? s_CapsuleColliderGetIsTrigger;
        private static SetBoolFn? s_CapsuleColliderSetIsTrigger;
        private static GetBoolFn? s_CapsuleColliderGetEnabled;
        private static SetBoolFn? s_CapsuleColliderSetEnabled;

        private static StaticMeshGetMeshFn? s_MeshColliderGetMesh;
        private static StaticMeshSetMeshFn? s_MeshColliderSetMesh;
        private static GetVec3Fn? s_MeshColliderGetOffset;
        private static SetVec3Fn? s_MeshColliderSetOffset;
        private static GetBoolFn? s_MeshColliderGetIsTrigger;
        private static SetBoolFn? s_MeshColliderSetIsTrigger;
        private static GetBoolFn? s_MeshColliderGetEnabled;
        private static SetBoolFn? s_MeshColliderSetEnabled;

        private static AINameAndJsonFn? s_AI_RegisterStateTree;
        private static AINameFn? s_AI_UnregisterStateTree;
        private static AINameFn? s_AI_StateTreeExists;

        private static AIEntityKeyBoolFn? s_AIBlackboardHasKey;
        private static AIEntityKeyActionFn? s_AIBlackboardRemoveKey;
        private static EntityActionFn? s_AIBlackboardClear;
        private static AIEntityKeyFloatFn? s_AIBlackboardGetFloat;
        private static AIEntityKeySetFloatFn? s_AIBlackboardSetFloat;
        private static AIEntityKeyLongFn? s_AIBlackboardGetInt;
        private static AIEntityKeySetLongFn? s_AIBlackboardSetInt;
        private static AIEntityKeyByteFn? s_AIBlackboardGetBool;
        private static AIEntityKeySetByteFn? s_AIBlackboardSetBool;
        private static AIEntityKeyVec3Fn? s_AIBlackboardGetVec3;
        private static AIEntityKeySetVec3Fn? s_AIBlackboardSetVec3;
        private static AIEntityKeyEntityFn? s_AIBlackboardGetEntity;
        private static AIEntityKeySetEntityFn? s_AIBlackboardSetEntity;
        private static AIEntityKeyStringFn? s_AIBlackboardGetString;
        private static AIEntityKeySetStringFn? s_AIBlackboardSetString;

        private static AIEntityStringFn? s_AIStateMachineGetTreeName;
        private static AISetEntityStringFn? s_AIStateMachineSetTreeName;
        private static AIEntityStringFn? s_AIStateMachineGetCurrentState;
        private static AIEntityStringFn? s_AIStateMachineGetPreviousState;
        private static GetFloatFn? s_AIStateMachineGetTimeInState;
        private static AIEntityNameFn? s_AIStateMachineRequestState;
        private static EntityActionFn? s_AIStateMachineResetRuntime;

        private static GetVec3Fn? s_AIControllerGetDesiredVelocity;
        private static SetVec3Fn? s_AIControllerSetDesiredVelocity;
        private static GetVec3Fn? s_AIControllerGetLookAtTarget;
        private static SetVec3Fn? s_AIControllerSetLookAtTarget;
        private static GetBoolFn? s_AIControllerGetWantsToAttack;
        private static SetBoolFn? s_AIControllerSetWantsToAttack;
        private static GetBoolFn? s_AIControllerGetEnabled;
        private static SetBoolFn? s_AIControllerSetEnabled;

        private static GetFloatFn? s_AIPerceptionGetSightRadius;
        private static SetFloatFn? s_AIPerceptionSetSightRadius;
        private static GetFloatFn? s_AIPerceptionGetLoseRadius;
        private static SetFloatFn? s_AIPerceptionSetLoseRadius;
        private static GetFloatFn? s_AIPerceptionGetFieldOfView;
        private static SetFloatFn? s_AIPerceptionSetFieldOfView;
        private static GetFloatFn? s_AIPerceptionGetEyeHeight;
        private static SetFloatFn? s_AIPerceptionSetEyeHeight;
        private static GetFloatFn? s_AIPerceptionGetInterval;
        private static SetFloatFn? s_AIPerceptionSetInterval;
        private static GetBoolFn? s_AIPerceptionGetRequireLineOfSight;
        private static SetBoolFn? s_AIPerceptionSetRequireLineOfSight;
        private static GetIntFn? s_AIPerceptionGetFactionId;
        private static SetIntFn? s_AIPerceptionSetFactionId;

        private static GetIntFn? s_AITargetableGetFactionId;
        private static SetIntFn? s_AITargetableSetFactionId;

        // ====================================================================
        // Initialization - called from C++ to provide function pointers
        // ====================================================================

        /// <summary>
        /// Called from C++ to set all native function pointers.
        /// The nativeCallbacks pointer points to a struct of function pointers.
        /// </summary>
        [UnmanagedCallersOnly]
        public static void SetNativeCallbacks(IntPtr nativeCallbacks)
        {
            int offset = 0;
            IntPtr ReadNext()
            {
                IntPtr ptr = Marshal.ReadIntPtr(nativeCallbacks, offset);
                offset += IntPtr.Size;
                return ptr;
            }

            s_NativeLog = Marshal.GetDelegateForFunctionPointer<NativeLogFn>(ReadNext());
            s_EntityHasComponent = Marshal.GetDelegateForFunctionPointer<EntityHasComponentFn>(ReadNext());
            s_EntityAddComponent = Marshal.GetDelegateForFunctionPointer<EntityAddComponentFn>(ReadNext());
            s_EntityFindByName = Marshal.GetDelegateForFunctionPointer<EntityFindByNameFn>(ReadNext());
            s_EntityInstantiate = Marshal.GetDelegateForFunctionPointer<EntityInstantiateFn>(ReadNext());

            s_TransformGetTranslation = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_TransformSetTranslation = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_TransformGetRotation = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_TransformSetRotation = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_TransformGetScale = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_TransformSetScale = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());

            s_StaticMeshGetMesh = Marshal.GetDelegateForFunctionPointer<StaticMeshGetMeshFn>(ReadNext());
            s_StaticMeshSetMesh = Marshal.GetDelegateForFunctionPointer<StaticMeshSetMeshFn>(ReadNext());

            s_RigidbodyGetVelocity = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_RigidbodySetVelocity = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_Rigidbody3DApplyImpulse = Marshal.GetDelegateForFunctionPointer<Rigidbody3DApplyImpulseFn>(ReadNext());
            s_Rigidbody3DApplyImpulseAtPoint = Marshal.GetDelegateForFunctionPointer<Rigidbody3DApplyImpulseAtPointFn>(ReadNext());

            s_TextComponentSetText = Marshal.GetDelegateForFunctionPointer<SetStringFn>(ReadNext());
            s_TextComponentGetText = Marshal.GetDelegateForFunctionPointer<GetStringFn>(ReadNext());
            s_TextComponentSetColor = Marshal.GetDelegateForFunctionPointer<SetVec4Fn>(ReadNext());
            s_TextComponentGetColor = Marshal.GetDelegateForFunctionPointer<GetVec4Fn>(ReadNext());
            s_TextComponentSetFontSize = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_TextComponentGetFontSize = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_TextComponentSetFontPath = Marshal.GetDelegateForFunctionPointer<SetStringFn>(ReadNext());
            s_TextComponentGetFontPath = Marshal.GetDelegateForFunctionPointer<GetStringFn>(ReadNext());

            s_InputIsKeyDown = Marshal.GetDelegateForFunctionPointer<IsKeyDownFn>(ReadNext());
            s_InputIsMouseButtonDown = Marshal.GetDelegateForFunctionPointer<IsMouseButtonDownFn>(ReadNext());

            s_AudioSource2DPlay = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AudioSource2DStop = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AudioSource2DSetVolume = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AudioSource2DGetVolume = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AudioSource2DSetLooping = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AudioSource2DIsLooping = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());

            s_AudioSource3DPlay = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AudioSource3DStop = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AudioSource3DSetVolume = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AudioSource3DGetVolume = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AudioSource3DSetLooping = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AudioSource3DIsLooping = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());

            s_PhysicsRaycast = Marshal.GetDelegateForFunctionPointer<PhysicsRaycastFn>(ReadNext());
            s_PhysicsOverlapBox = Marshal.GetDelegateForFunctionPointer<PhysicsOverlapBoxFn>(ReadNext());
            s_PhysicsOverlapBoxForEntity = Marshal.GetDelegateForFunctionPointer<PhysicsOverlapBoxForEntityFn>(ReadNext());
            s_PhysicsOverlapSphere = Marshal.GetDelegateForFunctionPointer<PhysicsOverlapSphereFn>(ReadNext());
            s_PhysicsOverlapSphereForEntity = Marshal.GetDelegateForFunctionPointer<PhysicsOverlapSphereForEntityFn>(ReadNext());
            s_EntityInstantiatePrefab = Marshal.GetDelegateForFunctionPointer<EntityInstantiatePrefabFn>(ReadNext());

            s_AnimatorPlay = Marshal.GetDelegateForFunctionPointer<AnimatorPlayFn>(ReadNext());
            s_AnimatorPlayByName = Marshal.GetDelegateForFunctionPointer<AnimatorPlayByNameFn>(ReadNext());
            s_AnimatorStop = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AnimatorPause = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AnimatorGetTime = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AnimatorSetTime = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AnimatorGetSpeed = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AnimatorSetSpeed = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AnimatorIsPlaying = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AnimatorSetPlaying = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AnimatorIsLooping = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AnimatorSetLooping = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AnimatorGetPlayOnStart = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AnimatorSetPlayOnStart = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AnimatorGetActiveClip = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_AnimatorSetActiveClip = Marshal.GetDelegateForFunctionPointer<SetIntFn>(ReadNext());
            s_AnimatorGetClipCount = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_AnimatorGetClipDuration = Marshal.GetDelegateForFunctionPointer<AnimatorGetClipDurationFn>(ReadNext());
            s_AnimatorCrossFade = Marshal.GetDelegateForFunctionPointer<AnimatorCrossFadeFn>(ReadNext());
            s_AnimatorCrossFadeByName = Marshal.GetDelegateForFunctionPointer<AnimatorCrossFadeByNameFn>(ReadNext());

            s_SocketAttach = Marshal.GetDelegateForFunctionPointer<SocketAttachFn>(ReadNext());
            s_SocketDetach = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());

            s_RagdollEnable = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_RagdollDisable = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_RagdollIsRagdoll = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_RagdollGetState = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_RagdollGetWeight = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RagdollGetBlendDuration = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RagdollSetBlendDuration = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_RagdollGetStartInRagdoll = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_RagdollSetStartInRagdoll = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_RagdollGetJointRadiusScale = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RagdollSetJointRadiusScale = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());

            s_RigidbodyGetBodyType = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_RigidbodySetBodyType = Marshal.GetDelegateForFunctionPointer<SetIntFn>(ReadNext());
            s_RigidbodyGetMass = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RigidbodySetMass = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_RigidbodyGetAngularVelocity = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_RigidbodySetAngularVelocity = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_RigidbodyGetUseGravity = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_RigidbodySetUseGravity = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_RigidbodyGetFriction = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RigidbodySetFriction = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_RigidbodyGetRestitution = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_RigidbodySetRestitution = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());

            s_BoxColliderGetSize = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_BoxColliderSetSize = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_BoxColliderGetOffset = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_BoxColliderSetOffset = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_BoxColliderGetIsTrigger = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_BoxColliderSetIsTrigger = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_BoxColliderGetEnabled = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_BoxColliderSetEnabled = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());

            s_SphereColliderGetRadius = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_SphereColliderSetRadius = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_SphereColliderGetOffset = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_SphereColliderSetOffset = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_SphereColliderGetIsTrigger = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_SphereColliderSetIsTrigger = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_SphereColliderGetEnabled = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_SphereColliderSetEnabled = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());

            s_CapsuleColliderGetRadius = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_CapsuleColliderSetRadius = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_CapsuleColliderGetHeight = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_CapsuleColliderSetHeight = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_CapsuleColliderGetOffset = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_CapsuleColliderSetOffset = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_CapsuleColliderGetIsTrigger = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_CapsuleColliderSetIsTrigger = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_CapsuleColliderGetEnabled = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_CapsuleColliderSetEnabled = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());

            s_MeshColliderGetMesh = Marshal.GetDelegateForFunctionPointer<StaticMeshGetMeshFn>(ReadNext());
            s_MeshColliderSetMesh = Marshal.GetDelegateForFunctionPointer<StaticMeshSetMeshFn>(ReadNext());
            s_MeshColliderGetOffset = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_MeshColliderSetOffset = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_MeshColliderGetIsTrigger = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_MeshColliderSetIsTrigger = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_MeshColliderGetEnabled = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_MeshColliderSetEnabled = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());

            s_AI_RegisterStateTree = Marshal.GetDelegateForFunctionPointer<AINameAndJsonFn>(ReadNext());
            s_AI_UnregisterStateTree = Marshal.GetDelegateForFunctionPointer<AINameFn>(ReadNext());
            s_AI_StateTreeExists = Marshal.GetDelegateForFunctionPointer<AINameFn>(ReadNext());

            s_AIBlackboardHasKey = Marshal.GetDelegateForFunctionPointer<AIEntityKeyBoolFn>(ReadNext());
            s_AIBlackboardRemoveKey = Marshal.GetDelegateForFunctionPointer<AIEntityKeyActionFn>(ReadNext());
            s_AIBlackboardClear = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());
            s_AIBlackboardGetFloat = Marshal.GetDelegateForFunctionPointer<AIEntityKeyFloatFn>(ReadNext());
            s_AIBlackboardSetFloat = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetFloatFn>(ReadNext());
            s_AIBlackboardGetInt = Marshal.GetDelegateForFunctionPointer<AIEntityKeyLongFn>(ReadNext());
            s_AIBlackboardSetInt = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetLongFn>(ReadNext());
            s_AIBlackboardGetBool = Marshal.GetDelegateForFunctionPointer<AIEntityKeyByteFn>(ReadNext());
            s_AIBlackboardSetBool = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetByteFn>(ReadNext());
            s_AIBlackboardGetVec3 = Marshal.GetDelegateForFunctionPointer<AIEntityKeyVec3Fn>(ReadNext());
            s_AIBlackboardSetVec3 = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetVec3Fn>(ReadNext());
            s_AIBlackboardGetEntity = Marshal.GetDelegateForFunctionPointer<AIEntityKeyEntityFn>(ReadNext());
            s_AIBlackboardSetEntity = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetEntityFn>(ReadNext());
            s_AIBlackboardGetString = Marshal.GetDelegateForFunctionPointer<AIEntityKeyStringFn>(ReadNext());
            s_AIBlackboardSetString = Marshal.GetDelegateForFunctionPointer<AIEntityKeySetStringFn>(ReadNext());

            s_AIStateMachineGetTreeName = Marshal.GetDelegateForFunctionPointer<AIEntityStringFn>(ReadNext());
            s_AIStateMachineSetTreeName = Marshal.GetDelegateForFunctionPointer<AISetEntityStringFn>(ReadNext());
            s_AIStateMachineGetCurrentState = Marshal.GetDelegateForFunctionPointer<AIEntityStringFn>(ReadNext());
            s_AIStateMachineGetPreviousState = Marshal.GetDelegateForFunctionPointer<AIEntityStringFn>(ReadNext());
            s_AIStateMachineGetTimeInState = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIStateMachineRequestState = Marshal.GetDelegateForFunctionPointer<AIEntityNameFn>(ReadNext());
            s_AIStateMachineResetRuntime = Marshal.GetDelegateForFunctionPointer<EntityActionFn>(ReadNext());

            s_AIControllerGetDesiredVelocity = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_AIControllerSetDesiredVelocity = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_AIControllerGetLookAtTarget = Marshal.GetDelegateForFunctionPointer<GetVec3Fn>(ReadNext());
            s_AIControllerSetLookAtTarget = Marshal.GetDelegateForFunctionPointer<SetVec3Fn>(ReadNext());
            s_AIControllerGetWantsToAttack = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AIControllerSetWantsToAttack = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AIControllerGetEnabled = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AIControllerSetEnabled = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());

            s_AIPerceptionGetSightRadius = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIPerceptionSetSightRadius = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AIPerceptionGetLoseRadius = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIPerceptionSetLoseRadius = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AIPerceptionGetFieldOfView = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIPerceptionSetFieldOfView = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AIPerceptionGetEyeHeight = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIPerceptionSetEyeHeight = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AIPerceptionGetInterval = Marshal.GetDelegateForFunctionPointer<GetFloatFn>(ReadNext());
            s_AIPerceptionSetInterval = Marshal.GetDelegateForFunctionPointer<SetFloatFn>(ReadNext());
            s_AIPerceptionGetRequireLineOfSight = Marshal.GetDelegateForFunctionPointer<GetBoolFn>(ReadNext());
            s_AIPerceptionSetRequireLineOfSight = Marshal.GetDelegateForFunctionPointer<SetBoolFn>(ReadNext());
            s_AIPerceptionGetFactionId = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_AIPerceptionSetFactionId = Marshal.GetDelegateForFunctionPointer<SetIntFn>(ReadNext());

            s_AITargetableGetFactionId = Marshal.GetDelegateForFunctionPointer<GetIntFn>(ReadNext());
            s_AITargetableSetFactionId = Marshal.GetDelegateForFunctionPointer<SetIntFn>(ReadNext());
        }

        // ====================================================================
        // Public API (called by C# scripts)
        // ====================================================================

        internal static void NativeLog(string message)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(message);
            try { s_NativeLog?.Invoke(ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static bool Entity_HasComponent(ulong id, Type componentType)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(componentType.FullName ?? componentType.Name);
            try { return s_EntityHasComponent?.Invoke(id, ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void Entity_AddComponent(ulong id, Type componentType)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(componentType.FullName ?? componentType.Name);
            try { s_EntityAddComponent?.Invoke(id, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static ulong Entity_FindEntityByName(string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { return s_EntityFindByName?.Invoke(ptr) ?? 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static object? Entity_GetScriptInstance(ulong entityID, Type type)
        {
            return ScriptGlue.GetInstance(entityID, type);
        }

        internal static ulong Entity_Instantiate(string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { return s_EntityInstantiate?.Invoke(ptr) ?? 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static ulong Entity_InstantiatePrefab(ulong prefabHandle)
        {
            return s_EntityInstantiatePrefab?.Invoke(prefabHandle) ?? 0;
        }

        // ----------------------------- TransformComponent -----------------------------

        internal static void TransformComponent_GetTranslation(ulong entityID, out Vector3 translation)
        {
            translation = default;
            s_TransformGetTranslation?.Invoke(entityID, out translation);
        }

        internal static void TransformComponent_SetTranslation(ulong entityID, ref Vector3 translation)
        {
            s_TransformSetTranslation?.Invoke(entityID, ref translation);
        }

        internal static void TransformComponent_GetRotation(ulong entityID, out Vector3 rotation)
        {
            rotation = default;
            s_TransformGetRotation?.Invoke(entityID, out rotation);
        }

        internal static void TransformComponent_SetRotation(ulong entityID, ref Vector3 rotation)
        {
            s_TransformSetRotation?.Invoke(entityID, ref rotation);
        }

        internal static void TransformComponent_GetScale(ulong entityID, out Vector3 scale)
        {
            scale = default;
            s_TransformGetScale?.Invoke(entityID, out scale);
        }

        internal static void TransformComponent_SetScale(ulong entityID, ref Vector3 scale)
        {
            s_TransformSetScale?.Invoke(entityID, ref scale);
        }

        // ----------------------------- StaticMeshComponent ------------------------------

        internal static void StaticMeshComponent_GetMesh(ulong entityID, out MeshRef mesh)
        {
            mesh = default;
            s_StaticMeshGetMesh?.Invoke(entityID, out mesh);
        }

        internal static void StaticMeshComponent_SetMesh(ulong entityID, MeshRef mesh)
        {
            s_StaticMeshSetMesh?.Invoke(entityID, mesh);
        }

        // ----------------------------- Rigidbody3DComponent -----------------------------

        internal static void Rigidbody3DComponent_GetVelocity(ulong entityID, out Vector3 velocity)
        {
            velocity = default;
            s_RigidbodyGetVelocity?.Invoke(entityID, out velocity);
        }

        internal static void Rigidbody3DComponent_SetVelocity(ulong entityID, ref Vector3 velocity)
        {
            s_RigidbodySetVelocity?.Invoke(entityID, ref velocity);
        }

        internal static void Rigidbody3DComponent_ApplyImpulse(ulong entityID, ref Vector3 impulse)
        {
            s_Rigidbody3DApplyImpulse?.Invoke(entityID, ref impulse);
        }

        internal static void Rigidbody3DComponent_ApplyImpulseAtPoint(ulong entityID, ref Vector3 impulse, ref Vector3 point)
        {
            s_Rigidbody3DApplyImpulseAtPoint?.Invoke(entityID, ref impulse, ref point);
        }

        // ----------------------------- TextComponent --------------------------------------

        internal static void TextComponent_SetText(ulong entityID, string text)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(text);
            try { s_TextComponentSetText?.Invoke(entityID, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static string TextComponent_GetText(ulong entityID)
        {
            IntPtr ptr = s_TextComponentGetText?.Invoke(entityID) ?? IntPtr.Zero;
            return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? string.Empty : string.Empty;
        }

        internal static void TextComponent_SetColor(ulong entityID, ref Vector4 color)
        {
            s_TextComponentSetColor?.Invoke(entityID, ref color);
        }

        internal static void TextComponent_GetColor(ulong entityID, out Vector4 color)
        {
            color = default;
            s_TextComponentGetColor?.Invoke(entityID, out color);
        }

        internal static void TextComponent_SetFontSize(ulong entityID, float size)
        {
            s_TextComponentSetFontSize?.Invoke(entityID, size);
        }

        internal static float TextComponent_GetFontSize(ulong entityID)
        {
            return s_TextComponentGetFontSize?.Invoke(entityID) ?? 0.0f;
        }

        internal static void TextComponent_SetFontPath(ulong entityID, string path)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(path);
            try { s_TextComponentSetFontPath?.Invoke(entityID, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static string TextComponent_GetFontPath(ulong entityID)
        {
            IntPtr ptr = s_TextComponentGetFontPath?.Invoke(entityID) ?? IntPtr.Zero;
            return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? string.Empty : string.Empty;
        }

        // ----------------------------- Input -----------------------------

        internal static bool Input_IsKeyDown(KeyCode key)
        {
            return s_InputIsKeyDown?.Invoke((int)key) != 0;
        }

        internal static bool Input_IsMouseButtonDown(MouseButton mouseCode)
        {
            return s_InputIsMouseButtonDown?.Invoke((int)mouseCode) != 0;
        }

        // ------------------------- AudioSource2DComponent --------------------------

        internal static void AudioSource2DComponent_Play(ulong entityID)
        {
            s_AudioSource2DPlay?.Invoke(entityID);
        }

        internal static void AudioSource2DComponent_Stop(ulong entityID)
        {
            s_AudioSource2DStop?.Invoke(entityID);
        }

        internal static void AudioSource2DComponent_SetVolume(ulong entityID, float volume)
        {
            s_AudioSource2DSetVolume?.Invoke(entityID, volume);
        }

        internal static float AudioSource2DComponent_GetVolume(ulong entityID)
        {
            return s_AudioSource2DGetVolume?.Invoke(entityID) ?? 0.0f;
        }

        internal static void AudioSource2DComponent_SetLooping(ulong entityID, bool looping)
        {
            s_AudioSource2DSetLooping?.Invoke(entityID, looping ? (byte)1 : (byte)0);
        }

        internal static bool AudioSource2DComponent_IsLooping(ulong entityID)
        {
            return s_AudioSource2DIsLooping?.Invoke(entityID) != 0;
        }

        // ------------------------- AudioSource3DComponent --------------------------

        internal static void AudioSource3DComponent_Play(ulong entityID)
        {
            s_AudioSource3DPlay?.Invoke(entityID);
        }

        internal static void AudioSource3DComponent_Stop(ulong entityID)
        {
            s_AudioSource3DStop?.Invoke(entityID);
        }

        internal static void AudioSource3DComponent_SetVolume(ulong entityID, float volume)
        {
            s_AudioSource3DSetVolume?.Invoke(entityID, volume);
        }

        internal static float AudioSource3DComponent_GetVolume(ulong entityID)
        {
            return s_AudioSource3DGetVolume?.Invoke(entityID) ?? 0.0f;
        }

        internal static void AudioSource3DComponent_SetLooping(ulong entityID, bool looping)
        {
            s_AudioSource3DSetLooping?.Invoke(entityID, looping ? (byte)1 : (byte)0);
        }

        internal static bool AudioSource3DComponent_IsLooping(ulong entityID)
        {
            return s_AudioSource3DIsLooping?.Invoke(entityID) != 0;
        }

        internal static bool Physics_Raycast(ref Vector3 origin, ref Vector3 direction, float maxDistance, out Vector3 hitPoint, out Vector3 hitNormal, out ulong entityID)
        {
            hitPoint = Vector3.Zero;
            hitNormal = Vector3.Zero;
            entityID = 0;

            return s_PhysicsRaycast?.Invoke(ref origin, ref direction, maxDistance, out hitPoint, out hitNormal, out entityID) != 0;
        }

        internal static int Physics_OverlapBox(ref Vector3 center, ref Vector3 halfExtents, IntPtr outEntities, int maxEntities)
            => s_PhysicsOverlapBox?.Invoke(ref center, ref halfExtents, outEntities, maxEntities) ?? 0;

        internal static int Physics_OverlapBoxForEntity(ulong entityID, ref Vector3 localCenter, ref Vector3 localHalfExtents, ulong ignoreEntityID, IntPtr outEntities, int maxEntities)
            => s_PhysicsOverlapBoxForEntity?.Invoke(entityID, ref localCenter, ref localHalfExtents, ignoreEntityID, outEntities, maxEntities) ?? 0;

        internal static int Physics_OverlapSphere(ref Vector3 center, float radius, IntPtr outEntities, int maxEntities)
            => s_PhysicsOverlapSphere?.Invoke(ref center, radius, outEntities, maxEntities) ?? 0;

        internal static int Physics_OverlapSphereForEntity(ulong entityID, ref Vector3 localCenter, float radius, ulong ignoreEntityID, IntPtr outEntities, int maxEntities)
            => s_PhysicsOverlapSphereForEntity?.Invoke(entityID, ref localCenter, radius, ignoreEntityID, outEntities, maxEntities) ?? 0;

        // ----------------------------- AnimatorComponent -----------------------------

        internal static void AnimatorComponent_Play(ulong entityID, int clipIndex)
        {
            s_AnimatorPlay?.Invoke(entityID, clipIndex);
        }

        internal static void AnimatorComponent_PlayByName(ulong entityID, string clipName)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(clipName);
            try { s_AnimatorPlayByName?.Invoke(entityID, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AnimatorComponent_Stop(ulong entityID)
        {
            s_AnimatorStop?.Invoke(entityID);
        }

        internal static void AnimatorComponent_Pause(ulong entityID)
        {
            s_AnimatorPause?.Invoke(entityID);
        }

        internal static float AnimatorComponent_GetTime(ulong entityID)
        {
            return s_AnimatorGetTime?.Invoke(entityID) ?? 0.0f;
        }

        internal static void AnimatorComponent_SetTime(ulong entityID, float time)
        {
            s_AnimatorSetTime?.Invoke(entityID, time);
        }

        internal static float AnimatorComponent_GetSpeed(ulong entityID)
        {
            return s_AnimatorGetSpeed?.Invoke(entityID) ?? 1.0f;
        }

        internal static void AnimatorComponent_SetSpeed(ulong entityID, float speed)
        {
            s_AnimatorSetSpeed?.Invoke(entityID, speed);
        }

        internal static bool AnimatorComponent_IsPlaying(ulong entityID)
        {
            return s_AnimatorIsPlaying?.Invoke(entityID) != 0;
        }

        internal static void AnimatorComponent_SetPlaying(ulong entityID, bool playing)
        {
            s_AnimatorSetPlaying?.Invoke(entityID, playing ? (byte)1 : (byte)0);
        }

        internal static bool AnimatorComponent_IsLooping(ulong entityID)
        {
            return s_AnimatorIsLooping?.Invoke(entityID) != 0;
        }

        internal static void AnimatorComponent_SetLooping(ulong entityID, bool looping)
        {
            s_AnimatorSetLooping?.Invoke(entityID, looping ? (byte)1 : (byte)0);
        }

        internal static bool AnimatorComponent_GetPlayOnStart(ulong entityID)
        {
            return s_AnimatorGetPlayOnStart?.Invoke(entityID) != 0;
        }

        internal static void AnimatorComponent_SetPlayOnStart(ulong entityID, bool playOnStart)
        {
            s_AnimatorSetPlayOnStart?.Invoke(entityID, playOnStart ? (byte)1 : (byte)0);
        }

        internal static int AnimatorComponent_GetActiveClip(ulong entityID)
        {
            return s_AnimatorGetActiveClip?.Invoke(entityID) ?? -1;
        }

        internal static void AnimatorComponent_SetActiveClip(ulong entityID, int clipIndex)
        {
            s_AnimatorSetActiveClip?.Invoke(entityID, clipIndex);
        }

        internal static int AnimatorComponent_GetClipCount(ulong entityID)
        {
            return s_AnimatorGetClipCount?.Invoke(entityID) ?? 0;
        }

        internal static float AnimatorComponent_GetClipDuration(ulong entityID, int clipIndex)
        {
            return s_AnimatorGetClipDuration?.Invoke(entityID, clipIndex) ?? 0.0f;
        }

        internal static void AnimatorComponent_CrossFade(ulong entityID, int clipIndex, float duration, bool restart)
        {
            s_AnimatorCrossFade?.Invoke(entityID, clipIndex, duration, restart ? (byte)1 : (byte)0);
        }

        internal static void AnimatorComponent_CrossFadeByName(ulong entityID, string clipName, float duration, bool restart)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(clipName);
            try { s_AnimatorCrossFadeByName?.Invoke(entityID, ptr, duration, restart ? (byte)1 : (byte)0); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        // ----------------------------- SocketComponent -----------------------------

        internal static void Socket_Attach(ulong entityID, ulong skeletonEntityID, string jointName,
                                           ref Vector3 translation, ref Vector3 rotation, ref Vector3 scale)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(jointName);
            try { s_SocketAttach?.Invoke(entityID, skeletonEntityID, ptr, ref translation, ref rotation, ref scale); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void Socket_Detach(ulong entityID)
        {
            s_SocketDetach?.Invoke(entityID);
        }

        // ----------------------------- RagdollComponent -----------------------------

        internal static void RagdollComponent_EnableRagdoll(ulong entityID)
            => s_RagdollEnable?.Invoke(entityID);

        internal static void RagdollComponent_DisableRagdoll(ulong entityID)
            => s_RagdollDisable?.Invoke(entityID);

        internal static bool RagdollComponent_IsRagdoll(ulong entityID)
            => s_RagdollIsRagdoll?.Invoke(entityID) != 0;

        internal static int RagdollComponent_GetState(ulong entityID)
            => s_RagdollGetState?.Invoke(entityID) ?? 0;

        internal static float RagdollComponent_GetWeight(ulong entityID)
            => s_RagdollGetWeight?.Invoke(entityID) ?? 0.0f;

        internal static float RagdollComponent_GetBlendDuration(ulong entityID)
            => s_RagdollGetBlendDuration?.Invoke(entityID) ?? 0.0f;

        internal static void RagdollComponent_SetBlendDuration(ulong entityID, float duration)
            => s_RagdollSetBlendDuration?.Invoke(entityID, duration);

        internal static bool RagdollComponent_GetStartInRagdoll(ulong entityID)
            => s_RagdollGetStartInRagdoll?.Invoke(entityID) != 0;

        internal static void RagdollComponent_SetStartInRagdoll(ulong entityID, bool startInRagdoll)
            => s_RagdollSetStartInRagdoll?.Invoke(entityID, startInRagdoll ? (byte)1 : (byte)0);

        internal static float RagdollComponent_GetJointRadiusScale(ulong entityID)
            => s_RagdollGetJointRadiusScale?.Invoke(entityID) ?? 1.0f;

        internal static void RagdollComponent_SetJointRadiusScale(ulong entityID, float scale)
            => s_RagdollSetJointRadiusScale?.Invoke(entityID, scale);

        // ----------------------------- RigidBody3DComponent -----------------------------

        internal static int Rigidbody3DComponent_GetBodyType(ulong entityID)
            => s_RigidbodyGetBodyType?.Invoke(entityID) ?? 0;

        internal static void Rigidbody3DComponent_SetBodyType(ulong entityID, int bodyType)
            => s_RigidbodySetBodyType?.Invoke(entityID, bodyType);

        internal static float Rigidbody3DComponent_GetMass(ulong entityID)
            => s_RigidbodyGetMass?.Invoke(entityID) ?? 0.0f;

        internal static void Rigidbody3DComponent_SetMass(ulong entityID, float mass)
            => s_RigidbodySetMass?.Invoke(entityID, mass);

        internal static void Rigidbody3DComponent_GetAngularVelocity(ulong entityID, out Vector3 velocity)
        {
            velocity = default;
            s_RigidbodyGetAngularVelocity?.Invoke(entityID, out velocity);
        }

        internal static void Rigidbody3DComponent_SetAngularVelocity(ulong entityID, ref Vector3 velocity)
            => s_RigidbodySetAngularVelocity?.Invoke(entityID, ref velocity);

        internal static bool Rigidbody3DComponent_GetUseGravity(ulong entityID)
            => s_RigidbodyGetUseGravity?.Invoke(entityID) != 0;

        internal static void Rigidbody3DComponent_SetUseGravity(ulong entityID, bool useGravity)
            => s_RigidbodySetUseGravity?.Invoke(entityID, useGravity ? (byte)1 : (byte)0);

        internal static float Rigidbody3DComponent_GetFriction(ulong entityID)
            => s_RigidbodyGetFriction?.Invoke(entityID) ?? 0.0f;

        internal static void Rigidbody3DComponent_SetFriction(ulong entityID, float friction)
            => s_RigidbodySetFriction?.Invoke(entityID, friction);

        internal static float Rigidbody3DComponent_GetRestitution(ulong entityID)
            => s_RigidbodyGetRestitution?.Invoke(entityID) ?? 0.0f;

        internal static void Rigidbody3DComponent_SetRestitution(ulong entityID, float restitution)
            => s_RigidbodySetRestitution?.Invoke(entityID, restitution);

        // ----------------------------- BoxCollider3DComponent -----------------------------

        internal static void BoxCollider3DComponent_GetSize(ulong entityID, out Vector3 size)
        {
            size = default;
            s_BoxColliderGetSize?.Invoke(entityID, out size);
        }

        internal static void BoxCollider3DComponent_SetSize(ulong entityID, ref Vector3 size)
            => s_BoxColliderSetSize?.Invoke(entityID, ref size);

        internal static void BoxCollider3DComponent_GetOffset(ulong entityID, out Vector3 offset)
        {
            offset = default;
            s_BoxColliderGetOffset?.Invoke(entityID, out offset);
        }

        internal static void BoxCollider3DComponent_SetOffset(ulong entityID, ref Vector3 offset)
            => s_BoxColliderSetOffset?.Invoke(entityID, ref offset);

        internal static bool BoxCollider3DComponent_GetIsTrigger(ulong entityID)
            => s_BoxColliderGetIsTrigger?.Invoke(entityID) != 0;

        internal static void BoxCollider3DComponent_SetIsTrigger(ulong entityID, bool isTrigger)
            => s_BoxColliderSetIsTrigger?.Invoke(entityID, isTrigger ? (byte)1 : (byte)0);

        internal static bool BoxCollider3DComponent_GetEnabled(ulong entityID)
            => s_BoxColliderGetEnabled?.Invoke(entityID) != 0;

        internal static void BoxCollider3DComponent_SetEnabled(ulong entityID, bool enabled)
            => s_BoxColliderSetEnabled?.Invoke(entityID, enabled ? (byte)1 : (byte)0);

        // ----------------------------- SphereCollider3DComponent -----------------------------

        internal static float SphereCollider3DComponent_GetRadius(ulong entityID)
            => s_SphereColliderGetRadius?.Invoke(entityID) ?? 0.0f;

        internal static void SphereCollider3DComponent_SetRadius(ulong entityID, float radius)
            => s_SphereColliderSetRadius?.Invoke(entityID, radius);

        internal static void SphereCollider3DComponent_GetOffset(ulong entityID, out Vector3 offset)
        {
            offset = default;
            s_SphereColliderGetOffset?.Invoke(entityID, out offset);
        }

        internal static void SphereCollider3DComponent_SetOffset(ulong entityID, ref Vector3 offset)
            => s_SphereColliderSetOffset?.Invoke(entityID, ref offset);

        internal static bool SphereCollider3DComponent_GetIsTrigger(ulong entityID)
            => s_SphereColliderGetIsTrigger?.Invoke(entityID) != 0;

        internal static void SphereCollider3DComponent_SetIsTrigger(ulong entityID, bool isTrigger)
            => s_SphereColliderSetIsTrigger?.Invoke(entityID, isTrigger ? (byte)1 : (byte)0);

        internal static bool SphereCollider3DComponent_GetEnabled(ulong entityID)
            => s_SphereColliderGetEnabled?.Invoke(entityID) != 0;

        internal static void SphereCollider3DComponent_SetEnabled(ulong entityID, bool enabled)
            => s_SphereColliderSetEnabled?.Invoke(entityID, enabled ? (byte)1 : (byte)0);

        // ----------------------------- CapsuleCollider3DComponent -----------------------------

        internal static float CapsuleCollider3DComponent_GetRadius(ulong entityID)
            => s_CapsuleColliderGetRadius?.Invoke(entityID) ?? 0.0f;

        internal static void CapsuleCollider3DComponent_SetRadius(ulong entityID, float radius)
            => s_CapsuleColliderSetRadius?.Invoke(entityID, radius);

        internal static float CapsuleCollider3DComponent_GetHeight(ulong entityID)
            => s_CapsuleColliderGetHeight?.Invoke(entityID) ?? 0.0f;

        internal static void CapsuleCollider3DComponent_SetHeight(ulong entityID, float height)
            => s_CapsuleColliderSetHeight?.Invoke(entityID, height);

        internal static void CapsuleCollider3DComponent_GetOffset(ulong entityID, out Vector3 offset)
        {
            offset = default;
            s_CapsuleColliderGetOffset?.Invoke(entityID, out offset);
        }

        internal static void CapsuleCollider3DComponent_SetOffset(ulong entityID, ref Vector3 offset)
            => s_CapsuleColliderSetOffset?.Invoke(entityID, ref offset);

        internal static bool CapsuleCollider3DComponent_GetIsTrigger(ulong entityID)
            => s_CapsuleColliderGetIsTrigger?.Invoke(entityID) != 0;

        internal static void CapsuleCollider3DComponent_SetIsTrigger(ulong entityID, bool isTrigger)
            => s_CapsuleColliderSetIsTrigger?.Invoke(entityID, isTrigger ? (byte)1 : (byte)0);

        internal static bool CapsuleCollider3DComponent_GetEnabled(ulong entityID)
            => s_CapsuleColliderGetEnabled?.Invoke(entityID) != 0;

        internal static void CapsuleCollider3DComponent_SetEnabled(ulong entityID, bool enabled)
            => s_CapsuleColliderSetEnabled?.Invoke(entityID, enabled ? (byte)1 : (byte)0);

        // ----------------------------- MeshCollider3DComponent -----------------------------

        internal static void MeshCollider3DComponent_GetMesh(ulong entityID, out MeshRef mesh)
        {
            mesh = default;
            s_MeshColliderGetMesh?.Invoke(entityID, out mesh);
        }

        internal static void MeshCollider3DComponent_SetMesh(ulong entityID, MeshRef mesh)
            => s_MeshColliderSetMesh?.Invoke(entityID, mesh);

        internal static void MeshCollider3DComponent_GetOffset(ulong entityID, out Vector3 offset)
        {
            offset = default;
            s_MeshColliderGetOffset?.Invoke(entityID, out offset);
        }

        internal static void MeshCollider3DComponent_SetOffset(ulong entityID, ref Vector3 offset)
            => s_MeshColliderSetOffset?.Invoke(entityID, ref offset);

        internal static bool MeshCollider3DComponent_GetIsTrigger(ulong entityID)
            => s_MeshColliderGetIsTrigger?.Invoke(entityID) != 0;

        internal static void MeshCollider3DComponent_SetIsTrigger(ulong entityID, bool isTrigger)
            => s_MeshColliderSetIsTrigger?.Invoke(entityID, isTrigger ? (byte)1 : (byte)0);

        internal static bool MeshCollider3DComponent_GetEnabled(ulong entityID)
            => s_MeshColliderGetEnabled?.Invoke(entityID) != 0;

        internal static void MeshCollider3DComponent_SetEnabled(ulong entityID, bool enabled)
            => s_MeshColliderSetEnabled?.Invoke(entityID, enabled ? (byte)1 : (byte)0);

        // ----------------------------- AI -----------------------------

        internal static bool AI_RegisterStateTree(string name, string json)
        {
            IntPtr namePtr = Marshal.StringToHGlobalAnsi(name);
            IntPtr jsonPtr = Marshal.StringToHGlobalAnsi(json);
            try { return s_AI_RegisterStateTree?.Invoke(namePtr, jsonPtr) != 0; }
            finally
            {
                Marshal.FreeHGlobal(namePtr);
                Marshal.FreeHGlobal(jsonPtr);
            }
        }

        internal static bool AI_UnregisterStateTree(string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { return s_AI_UnregisterStateTree?.Invoke(ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static bool AI_StateTreeExists(string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { return s_AI_StateTreeExists?.Invoke(ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static bool AIBlackboard_HasKey(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { return s_AIBlackboardHasKey?.Invoke(entityID, ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_RemoveKey(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardRemoveKey?.Invoke(entityID, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_Clear(ulong entityID)
            => s_AIBlackboardClear?.Invoke(entityID);

        internal static float AIBlackboard_GetFloat(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { return s_AIBlackboardGetFloat?.Invoke(entityID, ptr) ?? 0.0f; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetFloat(ulong entityID, string key, float value)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardSetFloat?.Invoke(entityID, ptr, value); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static long AIBlackboard_GetInt(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { return s_AIBlackboardGetInt?.Invoke(entityID, ptr) ?? 0L; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetInt(ulong entityID, string key, long value)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardSetInt?.Invoke(entityID, ptr, value); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static bool AIBlackboard_GetBool(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { return s_AIBlackboardGetBool?.Invoke(entityID, ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetBool(ulong entityID, string key, bool value)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardSetBool?.Invoke(entityID, ptr, value ? (byte)1 : (byte)0); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_GetVec3(ulong entityID, string key, out Vector3 value)
        {
            value = default;
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardGetVec3?.Invoke(entityID, ptr, out value); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetVec3(ulong entityID, string key, ref Vector3 value)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardSetVec3?.Invoke(entityID, ptr, ref value); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static ulong AIBlackboard_GetEntity(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { return s_AIBlackboardGetEntity?.Invoke(entityID, ptr) ?? 0UL; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetEntity(ulong entityID, string key, ulong value)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try { s_AIBlackboardSetEntity?.Invoke(entityID, ptr, value); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static string AIBlackboard_GetString(ulong entityID, string key)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(key);
            try
            {
                IntPtr result = s_AIBlackboardGetString?.Invoke(entityID, ptr) ?? IntPtr.Zero;
                return result != IntPtr.Zero ? Marshal.PtrToStringAnsi(result) ?? string.Empty : string.Empty;
            }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIBlackboard_SetString(ulong entityID, string key, string value)
        {
            IntPtr keyPtr = Marshal.StringToHGlobalAnsi(key);
            IntPtr valuePtr = Marshal.StringToHGlobalAnsi(value);
            try { s_AIBlackboardSetString?.Invoke(entityID, keyPtr, valuePtr); }
            finally
            {
                Marshal.FreeHGlobal(keyPtr);
                Marshal.FreeHGlobal(valuePtr);
            }
        }

        internal static string AIStateMachine_GetTreeName(ulong entityID)
        {
            IntPtr ptr = s_AIStateMachineGetTreeName?.Invoke(entityID) ?? IntPtr.Zero;
            return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? string.Empty : string.Empty;
        }

        internal static void AIStateMachine_SetTreeName(ulong entityID, string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { s_AIStateMachineSetTreeName?.Invoke(entityID, ptr); }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static string AIStateMachine_GetCurrentState(ulong entityID)
        {
            IntPtr ptr = s_AIStateMachineGetCurrentState?.Invoke(entityID) ?? IntPtr.Zero;
            return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? string.Empty : string.Empty;
        }

        internal static string AIStateMachine_GetPreviousState(ulong entityID)
        {
            IntPtr ptr = s_AIStateMachineGetPreviousState?.Invoke(entityID) ?? IntPtr.Zero;
            return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) ?? string.Empty : string.Empty;
        }

        internal static float AIStateMachine_GetTimeInState(ulong entityID)
            => s_AIStateMachineGetTimeInState?.Invoke(entityID) ?? 0.0f;

        internal static bool AIStateMachine_RequestState(ulong entityID, string stateName)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(stateName);
            try { return s_AIStateMachineRequestState?.Invoke(entityID, ptr) != 0; }
            finally { Marshal.FreeHGlobal(ptr); }
        }

        internal static void AIStateMachine_ResetRuntime(ulong entityID)
            => s_AIStateMachineResetRuntime?.Invoke(entityID);

        internal static void AIController_GetDesiredVelocity(ulong entityID, out Vector3 value)
        {
            value = default;
            s_AIControllerGetDesiredVelocity?.Invoke(entityID, out value);
        }

        internal static void AIController_SetDesiredVelocity(ulong entityID, ref Vector3 value)
            => s_AIControllerSetDesiredVelocity?.Invoke(entityID, ref value);

        internal static void AIController_GetLookAtTarget(ulong entityID, out Vector3 value)
        {
            value = default;
            s_AIControllerGetLookAtTarget?.Invoke(entityID, out value);
        }

        internal static void AIController_SetLookAtTarget(ulong entityID, ref Vector3 value)
            => s_AIControllerSetLookAtTarget?.Invoke(entityID, ref value);

        internal static bool AIController_GetWantsToAttack(ulong entityID)
            => s_AIControllerGetWantsToAttack?.Invoke(entityID) != 0;

        internal static void AIController_SetWantsToAttack(ulong entityID, bool value)
            => s_AIControllerSetWantsToAttack?.Invoke(entityID, value ? (byte)1 : (byte)0);

        internal static bool AIController_GetEnabled(ulong entityID)
            => s_AIControllerGetEnabled?.Invoke(entityID) != 0;

        internal static void AIController_SetEnabled(ulong entityID, bool value)
            => s_AIControllerSetEnabled?.Invoke(entityID, value ? (byte)1 : (byte)0);

        internal static float AIPerception_GetSightRadius(ulong entityID)
            => s_AIPerceptionGetSightRadius?.Invoke(entityID) ?? 0.0f;

        internal static void AIPerception_SetSightRadius(ulong entityID, float value)
            => s_AIPerceptionSetSightRadius?.Invoke(entityID, value);

        internal static float AIPerception_GetLoseRadius(ulong entityID)
            => s_AIPerceptionGetLoseRadius?.Invoke(entityID) ?? 0.0f;

        internal static void AIPerception_SetLoseRadius(ulong entityID, float value)
            => s_AIPerceptionSetLoseRadius?.Invoke(entityID, value);

        internal static float AIPerception_GetFieldOfView(ulong entityID)
            => s_AIPerceptionGetFieldOfView?.Invoke(entityID) ?? 0.0f;

        internal static void AIPerception_SetFieldOfView(ulong entityID, float value)
            => s_AIPerceptionSetFieldOfView?.Invoke(entityID, value);

        internal static float AIPerception_GetEyeHeight(ulong entityID)
            => s_AIPerceptionGetEyeHeight?.Invoke(entityID) ?? 0.0f;

        internal static void AIPerception_SetEyeHeight(ulong entityID, float value)
            => s_AIPerceptionSetEyeHeight?.Invoke(entityID, value);

        internal static float AIPerception_GetInterval(ulong entityID)
            => s_AIPerceptionGetInterval?.Invoke(entityID) ?? 0.0f;

        internal static void AIPerception_SetInterval(ulong entityID, float value)
            => s_AIPerceptionSetInterval?.Invoke(entityID, value);

        internal static bool AIPerception_GetRequireLineOfSight(ulong entityID)
            => s_AIPerceptionGetRequireLineOfSight?.Invoke(entityID) != 0;

        internal static void AIPerception_SetRequireLineOfSight(ulong entityID, bool value)
            => s_AIPerceptionSetRequireLineOfSight?.Invoke(entityID, value ? (byte)1 : (byte)0);

        internal static int AIPerception_GetFactionId(ulong entityID)
            => s_AIPerceptionGetFactionId?.Invoke(entityID) ?? 0;

        internal static void AIPerception_SetFactionId(ulong entityID, int value)
            => s_AIPerceptionSetFactionId?.Invoke(entityID, value);

        internal static int AITargetable_GetFactionId(ulong entityID)
            => s_AITargetableGetFactionId?.Invoke(entityID) ?? 0;

        internal static void AITargetable_SetFactionId(ulong entityID, int value)
            => s_AITargetableSetFactionId?.Invoke(entityID, value);

    }
}

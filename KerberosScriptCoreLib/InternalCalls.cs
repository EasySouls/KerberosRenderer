using System;
using System.Runtime.InteropServices;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source
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

        // ====================================================================
        // Stored delegate instances (prevent GC collection)
        // ====================================================================

        private static NativeLogFn? s_NativeLog;
        private static EntityHasComponentFn? s_EntityHasComponent;
        private static EntityAddComponentFn? s_EntityAddComponent;
        private static EntityFindByNameFn? s_EntityFindByName;
        private static EntityInstantiateFn? s_EntityInstantiate;

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

        internal static object? Entity_GetScriptInstance(ulong entityID)
        {
            return ScriptGlue.GetInstance(entityID);
        }

        internal static ulong Entity_Instantiate(string name)
        {
            IntPtr ptr = Marshal.StringToHGlobalAnsi(name);
            try { return s_EntityInstantiate?.Invoke(ptr) ?? 0; }
            finally { Marshal.FreeHGlobal(ptr); }
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

    }
}

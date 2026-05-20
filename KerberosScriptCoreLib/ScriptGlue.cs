using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source
{
    /// <summary>
    /// Bridge between the C++ engine and the C# scripting system.
    /// Provides [UnmanagedCallersOnly] entry points for the .NET hosting API.
    /// </summary>
    public static class ScriptGlue
    {
        private static Assembly? s_CoreAssembly;
        private static Type? s_EntityBaseClass;
        private static readonly Dictionary<string, Type> EntityClasses = new();
        private static readonly Dictionary<ulong, Entity> EntityInstances = new();

        // Cached MethodInfo for OnCreate and OnUpdate
        private static readonly Dictionary<Type, MethodInfo?> OnCreateMethods = new();
        private static readonly Dictionary<Type, MethodInfo?> OnUpdateMethods = new();
        private static readonly Dictionary<Type, MethodInfo?> OnCollisionEnterMethods = new();
        private static readonly Dictionary<Type, MethodInfo?> OnCollisionPersistMethods = new();
        private static readonly Dictionary<Type, MethodInfo?> OnCollisionExitMethods = new();
        private static readonly Dictionary<Type, ConstructorInfo?> UlongConstructors = new();

        // ====================================================================
        // Assembly Loading
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int LoadAssemblyClasses(IntPtr assemblyPathPtr)
        {
            try
            {
                string assemblyPath = Marshal.PtrToStringAnsi(assemblyPathPtr)!;
                // This bridge executes from KerberosScriptCoreLib itself.
                // Loading the same DLL again via Assembly.LoadFrom can create a second
                // assembly identity, which breaks `constructed as Entity` casts.
                s_CoreAssembly = typeof(ScriptGlue).Assembly;
                EntityClasses.Clear();
                OnCreateMethods.Clear();
                OnUpdateMethods.Clear();
                UlongConstructors.Clear();
                EntityInstances.Clear();

                s_EntityBaseClass = s_CoreAssembly.GetType("Kerberos.Source.Kerberos.Scene.Entity");
                if (s_EntityBaseClass == null)
                    return 0;

                Logger.Log($"Assembly name from path: {s_CoreAssembly.FullName}");
                Logger.Log($"Assembly name from Entity base class: {s_EntityBaseClass.Assembly.FullName}");
                Console.Out.WriteLine($"Assembly name from path: {s_CoreAssembly.FullName}");
                Console.Out.WriteLine($"Assembly name from Entity base class: {s_EntityBaseClass.Assembly.FullName}");
                Console.Error.WriteLine($"[ScriptGlue] Loaded assembly: {s_CoreAssembly.FullName}");
                Console.Error.WriteLine($"[ScriptGlue] Requested assembly path: {assemblyPath}");

                foreach (var type in s_CoreAssembly.GetTypes())
                {
                    if (type.IsSubclassOf(s_EntityBaseClass) && !type.IsAbstract)
                    {
                        string fullName = type.FullName ?? type.Name;
                        EntityClasses[fullName] = type;

                        // Cache method lookups
                        OnCreateMethods[type] = type.GetMethod("OnCreate",
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
                        OnUpdateMethods[type] = type.GetMethod("OnUpdate",
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                            null, new[] { typeof(float) }, null);
                        OnCollisionEnterMethods[type] = type.GetMethod("OnCollisionEnter",
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                            null, new[] { typeof(Entity) }, null);
                        OnCollisionPersistMethods[type] = type.GetMethod("OnCollisionPersist",
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                            null, new[] { typeof(Entity) }, null);
                        OnCollisionExitMethods[type] = type.GetMethod("OnCollisionExit",
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                            null, new[] { typeof(Entity) }, null);
                        UlongConstructors[type] = type.GetConstructor(
                            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                            null, new[] { typeof(ulong) }, null);
                    }
                }

                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] LoadAssemblyClasses failed: {ex}");
                return 0;
            }
        }

        // ====================================================================
        // Class Queries
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int ClassExists(IntPtr classNamePtr)
        {
            string className = Marshal.PtrToStringAnsi(classNamePtr)!;
            return EntityClasses.ContainsKey(className) ? 1 : 0;
        }

        /// <summary>
        /// Gets the number of serializable (public) fields for a given class.
        /// </summary>
        [UnmanagedCallersOnly]
        public static int GetClassFieldCount(IntPtr classNamePtr)
        {
            string className = Marshal.PtrToStringAnsi(classNamePtr)!;
            if (!EntityClasses.TryGetValue(className, out var type))
                return 0;

            var fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public);
            return fields.Length;
        }

        /// <summary>
        /// Gets field info for a class. Writes field names and type names into the provided buffers.
        /// fieldNamesBuffer: pointer to array of IntPtr (each pointing to a char buffer)
        /// fieldTypeNamesBuffer: pointer to array of IntPtr (each pointing to a char buffer)
        /// bufferSize: size of each individual char buffer
        /// </summary>
        [UnmanagedCallersOnly]
        public static int GetClassFields(IntPtr classNamePtr, IntPtr fieldNamesBuffer, IntPtr fieldTypeNamesBuffer, int maxFields, int bufferSize)
        {
            try
            {
                string className = Marshal.PtrToStringAnsi(classNamePtr)!;
                if (!EntityClasses.TryGetValue(className, out var type))
                    return 0;

                var fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public);
                int count = Math.Min(fields.Length, maxFields);

                for (int i = 0; i < count; i++)
                {
                    IntPtr namePtr = Marshal.ReadIntPtr(fieldNamesBuffer, i * IntPtr.Size);
                    IntPtr typePtr = Marshal.ReadIntPtr(fieldTypeNamesBuffer, i * IntPtr.Size);

                    WriteStringToBuffer(fields[i].Name, namePtr, bufferSize);
                    WriteStringToBuffer(fields[i].FieldType.FullName ?? fields[i].FieldType.Name, typePtr, bufferSize);
                }

                return count;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] GetClassFields failed: {ex}");
                return 0;
            }
        }

        // ====================================================================
        // Instance Management
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int CreateInstance(ulong entityID, IntPtr classNamePtr)
        {
            try
            {
                string className = Marshal.PtrToStringAnsi(classNamePtr)!;
                if (!EntityClasses.TryGetValue(className, out var type))
                    return 0;

                Entity? instance;

                // Try constructor with ulong parameter first
                if (UlongConstructors.TryGetValue(type, out var ctor) && ctor != null)
                {
                    Console.WriteLine($"[ScriptGlue] Creating instance of {className} with ulong constructor");
                    object constructed = ctor.Invoke([entityID]);
                    instance = constructed as Entity;
                }
                else
                {
                    // Fall back to default constructor + set ID via reflection
                    Console.WriteLine($"[ScriptGlue] Creating instance of {className} with default constructor");
                    instance = (Entity?)Activator.CreateInstance(type, true);
                    if (instance != null)
                    {
                        var idField = typeof(Entity).GetField("ID", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                        idField?.SetValue(instance, entityID);
                    }
                }

                if (instance == null)
                {
                    Console.Error.WriteLine($"[ScriptGlue] Failed to create instance of {className}");
                    return 0;
                }

                EntityInstances[entityID] = instance;
                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] CreateInstance failed: {ex}");
                Console.Error.WriteLine(ex.StackTrace);
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static void DestroyInstance(ulong entityID)
        {
            EntityInstances.Remove(entityID);
        }

        [UnmanagedCallersOnly]
        public static void ClearInstances()
        {
            EntityInstances.Clear();
        }

        /// <summary>
        /// Gets a managed entity instance by ID. Used by InternalCalls.Entity_GetScriptInstance.
        /// </summary>
        public static Entity? GetInstance(ulong entityID)
        {
            EntityInstances.TryGetValue(entityID, out var instance);
            return instance;
        }

        // ====================================================================
        // Method Invocation
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int InvokeOnCreate(ulong entityID)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                var type = instance.GetType();
                if (OnCreateMethods.TryGetValue(type, out var method) && method != null)
                {
                    method.Invoke(instance, null);
                }

                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] InvokeOnCreate failed for entity {entityID}: {ex}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int InvokeOnUpdate(ulong entityID, float deltaTime)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                var type = instance.GetType();
                if (OnUpdateMethods.TryGetValue(type, out var method) && method != null)
                {
                    method.Invoke(instance, new object[] { deltaTime });
                }

                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] InvokeOnUpdate failed for entity {entityID}: {ex}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int InvokeOnCollisionEnter(ulong entityID, IntPtr eventPtr)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                var type = instance.GetType();
                if (OnCollisionEnterMethods.TryGetValue(type, out var method) && method != null)
                {
                    method.Invoke(instance, new object[] { eventPtr });
                }

                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] InvokeOnCollisionEnter failed for entity {entityID}: {ex}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int InvokeOnCollisionPersist(ulong entityID, IntPtr eventPtr)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;
                var type = instance.GetType();
                if (OnCollisionPersistMethods.TryGetValue(type, out var method) && method != null)
                {
                    method.Invoke(instance, new object[] { eventPtr });
                }
                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] InvokeOnCollisionPersist failed for entity {entityID}: {ex}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int InvokeOnCollisionExit(ulong entityID, IntPtr eventPtr)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;
                var type = instance.GetType();
                if (OnCollisionExitMethods.TryGetValue(type, out var method) && method != null)
                {
                    method.Invoke(instance, new object[] { eventPtr });
                }
                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[ScriptGlue] InvokeOnCollisionExit failed for entity {entityID}: {ex}");
                return 0;
            }
        }

        // ====================================================================
        // Field Access
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int GetFieldValueFloat(ulong entityID, IntPtr fieldNamePtr, IntPtr outValue)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                string fieldName = Marshal.PtrToStringAnsi(fieldNamePtr)!;
                var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.Public);
                if (field == null)
                    return 0;

                object? value = field.GetValue(instance);
                if (value is float f)
                {
                    Marshal.Copy(BitConverter.GetBytes(f), 0, outValue, sizeof(float));
                    return 1;
                }

                return 0;
            }
            catch
            {
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        public static int SetFieldValueFloat(ulong entityID, IntPtr fieldNamePtr, float value)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                string fieldName = Marshal.PtrToStringAnsi(fieldNamePtr)!;
                var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.Public);
                if (field == null)
                    return 0;

                field.SetValue(instance, value);
                return 1;
            }
            catch
            {
                return 0;
            }
        }

        /// <summary>
        /// Generic field getter that copies field value bytes to a buffer.
        /// Supports: int, float, double, long, bool, byte, short, and blittable structs (Vector2/3/4).
        /// </summary>
        [UnmanagedCallersOnly]
        public static int GetFieldValue(ulong entityID, IntPtr fieldNamePtr, IntPtr outValue, int bufferSize)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                string fieldName = Marshal.PtrToStringAnsi(fieldNamePtr)!;
                var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.Public);
                if (field == null)
                    return 0;

                object? value = field.GetValue(instance);
                if (value == null)
                    return 0;

                return MarshalValueToBuffer(value, outValue, bufferSize);
            }
            catch
            {
                return 0;
            }
        }

        /// <summary>
        /// Generic field setter that reads field value bytes from a buffer.
        /// </summary>
        [UnmanagedCallersOnly]
        public static int SetFieldValue(ulong entityID, IntPtr fieldNamePtr, IntPtr valuePtr, int valueSize)
        {
            try
            {
                if (!EntityInstances.TryGetValue(entityID, out var instance))
                    return 0;

                string fieldName = Marshal.PtrToStringAnsi(fieldNamePtr)!;
                var field = instance.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.Public);
                if (field == null)
                    return 0;

                object? value = UnmarshalValueFromBuffer(field.FieldType, valuePtr, valueSize);
                if (value == null)
                    return 0;

                field.SetValue(instance, value);
                return 1;
            }
            catch
            {
                return 0;
            }
        }

        // ====================================================================
        // Entity Class Enumeration (for editor)
        // ====================================================================

        [UnmanagedCallersOnly]
        public static int GetEntityClassCount()
        {
            return EntityClasses.Count;
        }

        /// <summary>
        /// Gets entity class names. Writes names into the provided buffer.
        /// </summary>
        [UnmanagedCallersOnly]
        public static int GetEntityClassNames(IntPtr namesBuffer, int maxNames, int bufferSize)
        {
            try
            {
                int count = 0;
                foreach (var name in EntityClasses.Keys)
                {
                    if (count >= maxNames)
                        break;

                    IntPtr namePtr = Marshal.ReadIntPtr(namesBuffer, count * IntPtr.Size);
                    WriteStringToBuffer(name, namePtr, bufferSize);
                    count++;
                }
                return count;
            }
            catch
            {
                return 0;
            }
        }

        // ====================================================================
        // Helpers
        // ====================================================================

        private static void WriteStringToBuffer(string str, IntPtr buffer, int bufferSize)
        {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(str);
            int len = Math.Min(bytes.Length, bufferSize - 1);
            Marshal.Copy(bytes, 0, buffer, len);
            Marshal.WriteByte(buffer, len, 0); // Null terminate
        }

        private static int MarshalValueToBuffer(object value, IntPtr buffer, int bufferSize)
        {
            switch (value)
            {
                case bool b:
                    if (bufferSize < 1) return 0;
                    Marshal.WriteByte(buffer, b ? (byte)1 : (byte)0);
                    return 1;
                case byte by:
                    if (bufferSize < 1) return 0;
                    Marshal.WriteByte(buffer, by);
                    return 1;
                case short s:
                    if (bufferSize < 2) return 0;
                    Marshal.WriteInt16(buffer, s);
                    return 1;
                case ushort us:
                    if (bufferSize < 2) return 0;
                    Marshal.WriteInt16(buffer, (short)us);
                    return 1;
                case int i:
                    if (bufferSize < 4) return 0;
                    Marshal.WriteInt32(buffer, i);
                    return 1;
                case uint ui:
                    if (bufferSize < 4) return 0;
                    Marshal.WriteInt32(buffer, (int)ui);
                    return 1;
                case long l:
                    if (bufferSize < 8) return 0;
                    Marshal.WriteInt64(buffer, l);
                    return 1;
                case ulong ul:
                    if (bufferSize < 8) return 0;
                    Marshal.WriteInt64(buffer, (long)ul);
                    return 1;
                case float f:
                    if (bufferSize < 4) return 0;
                    Marshal.Copy(BitConverter.GetBytes(f), 0, buffer, 4);
                    return 1;
                case double d:
                    if (bufferSize < 8) return 0;
                    Marshal.Copy(BitConverter.GetBytes(d), 0, buffer, 8);
                    return 1;
                default:
                    // Try to marshal as a blittable struct (Vector2, Vector3, Vector4)
                    try
                    {
                        int size = Marshal.SizeOf(value.GetType());
                        if (size > bufferSize) return 0;
                        Marshal.StructureToPtr(value, buffer, false);
                        return 1;
                    }
                    catch
                    {
                        return 0;
                    }
            }
        }

        private static object? UnmarshalValueFromBuffer(Type fieldType, IntPtr buffer, int bufferSize)
        {
            if (fieldType == typeof(bool))
                return Marshal.ReadByte(buffer) != 0;
            if (fieldType == typeof(byte))
                return Marshal.ReadByte(buffer);
            if (fieldType == typeof(short))
                return Marshal.ReadInt16(buffer);
            if (fieldType == typeof(ushort))
                return (ushort)Marshal.ReadInt16(buffer);
            if (fieldType == typeof(int))
                return Marshal.ReadInt32(buffer);
            if (fieldType == typeof(uint))
                return (uint)Marshal.ReadInt32(buffer);
            if (fieldType == typeof(long))
                return Marshal.ReadInt64(buffer);
            if (fieldType == typeof(ulong))
                return (ulong)Marshal.ReadInt64(buffer);
            if (fieldType == typeof(float))
            {
                byte[] bytes = new byte[4];
                Marshal.Copy(buffer, bytes, 0, 4);
                return BitConverter.ToSingle(bytes, 0);
            }
            if (fieldType == typeof(double))
            {
                byte[] bytes = new byte[8];
                Marshal.Copy(buffer, bytes, 0, 8);
                return BitConverter.ToDouble(bytes, 0);
            }

            // Try blittable struct
            try
            {
                return Marshal.PtrToStructure(buffer, fieldType);
            }
            catch
            {
                return null;
            }
        }
    }
}

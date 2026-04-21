using System;
using Kerberos.Source.Kerberos.Core;

namespace Kerberos.Source.Kerberos.Scene
{
    public class Entity
    {
        protected Entity()
        {
            ID = 0;
        }

        internal Entity(ulong id)
        {
            ID = id;
        }

        protected readonly ulong ID;

        protected virtual void OnCreate() { }

        protected virtual void OnUpdate(float deltaTime) { }

        public Vector3 Translation
        {
            get
            {
                InternalCalls.TransformComponent_GetTranslation(ID, out Vector3 translation);
                return translation;
            }
            protected set => InternalCalls.TransformComponent_SetTranslation(ID, ref value);
        }

        public ulong GetID() => ID;

        protected bool HasComponent<T>() where T : Component, new()
        {
            return InternalCalls.Entity_HasComponent(ID, typeof(T));
        }

        protected T GetComponent<T>() where T : Component, new()
        {
            if (!HasComponent<T>())
                throw new ArgumentNullException(nameof(T));

            T component = new T() { Entity = this };
            return component;
        }

        public void AddComponent<T>() where T : Component, new()
        {
            if (HasComponent<T>())
                throw new InvalidOperationException($"Entity already has a component of type {typeof(T).Name}");

            InternalCalls.Entity_AddComponent(ID, typeof(T));
        }

        protected static Entity? FindEntityByName(string name)
        {
            ulong entityID = InternalCalls.Entity_FindEntityByName(name);
            return entityID == 0 ? null : new Entity(entityID);
        }

        protected internal T As<T>() where T : Entity, new()
        {
            object instance = InternalCalls.Entity_GetScriptInstance(ID)
                              ?? throw new ArgumentNullException(nameof(ID));

            return (instance as T)!;
        }

        protected static Entity Instantiate(string name)
        {
            ulong entityID = InternalCalls.Entity_Instantiate(name);
            return new Entity(entityID);
        }
    }
}
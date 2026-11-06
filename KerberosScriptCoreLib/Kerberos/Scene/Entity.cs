using System;
using Kerberos.Core;

namespace Kerberos.Scene
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

        protected virtual void OnCollisionEnter(Collision collision) { }
        protected virtual void OnCollisionPersist(Collision collision) { }
        protected virtual void OnCollisionExit(Collision collision) { }
        protected virtual void OnAnimationEvent(AnimationEvent animationEvent) { }

        public Vector3 Translation
        {
            get
            {
                InternalCalls.TransformComponent_GetTranslation(ID, out Vector3 translation);
                return translation;
            }
            set => InternalCalls.TransformComponent_SetTranslation(ID, ref value);
        }

        public ulong GetID() => ID;

        /// <summary>
        /// Attaches this entity to a named joint of the skeleton entity. The skeleton pose drives
        /// the entity's world transform; the authored local transform is not modified.
        /// </summary>
        public void AttachToSocket(Entity skeletonEntity, string jointName)
            => AttachToSocket(skeletonEntity, jointName, new Vector3(0.0f), new Vector3(0.0f), new Vector3(1.0f));

        public void AttachToSocket(Entity skeletonEntity, string jointName,
                                   Vector3 localTranslation, Vector3 localRotation, Vector3 localScale)
        {
            if (skeletonEntity == null)
                throw new ArgumentNullException(nameof(skeletonEntity));
            if (string.IsNullOrWhiteSpace(jointName))
                throw new ArgumentException("Joint name must not be empty.", nameof(jointName));

            InternalCalls.Socket_Attach(ID, skeletonEntity.GetID(), jointName,
                                        ref localTranslation, ref localRotation, ref localScale);
        }

        public void DetachFromSocket()
        {
            InternalCalls.Socket_Detach(ID);
        }

        protected bool HasComponent<T>() where T : Component, new()
        {
            return InternalCalls.Entity_HasComponent(ID, typeof(T));
        }

        public T GetComponent<T>() where T : Component, new()
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
            object instance = InternalCalls.Entity_GetScriptInstance(ID, typeof(T))
                              ?? throw new ArgumentNullException(nameof(ID));

            return (instance as T)!;
        }

        /// <summary>
        /// Returns the script instance of the requested type attached to this entity,
        /// or null when the entity does not have that script.
        /// </summary>
        protected internal T? GetScript<T>() where T : Entity, new()
        {
            return InternalCalls.Entity_GetScriptInstance(ID, typeof(T)) as T;
        }

        protected static Entity Instantiate(string name)
        {
            ulong entityID = InternalCalls.Entity_Instantiate(name);
            return new Entity(entityID);
        }

        protected static Entity Instantiate(PrefabRef prefab)
        {
            ulong entityID = InternalCalls.Entity_InstantiatePrefab(prefab.Handle);
            return new Entity(entityID);
        }
    }
}
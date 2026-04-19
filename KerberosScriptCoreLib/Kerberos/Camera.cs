using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;
using System;

namespace Kerberos.Source.Kerberos
{
    public class Camera : Entity
    {
        public float DistanceFromPlayer;
        public float FOV = 45.0f;

        private TransformComponent _transform;
        private Entity? _target;

        public Camera() : base()
        {
        }

        public Camera(ulong id) : base(id)
        {
        }

        protected override void OnCreate()
        {
            _transform = GetComponent<TransformComponent>();
            _target = FindEntityByName("Player");
        }

        protected override void OnUpdate(float deltaTime)
        {
            if (_target != null)
            {
                Vector3 targetTranslation = _target.Translation;
                //Translation = new Vector3(targetTranslation.X + 3.0f, targetTranslation.Y + 2.0f, targetTranslation.Z - 5.0f);
                //Translation = new Vector3(Translation.X, targetTranslation.Y + 2.0f, Translation.Z);
                //_transform.LookAt(_target.Translation);
            }
            else
            {
                Logger.Log("Player is null, cannot follow");
            }
        }
    }
}
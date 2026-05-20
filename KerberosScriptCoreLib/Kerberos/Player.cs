using System;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source.Kerberos
{
    public class Player : Entity
    {
        public float Speed = 5.0f;
        public float ShootingCooldownTime = 2.0f;
        private float _shootingCooldownTimer = 0.0f;

        public MeshRef BulletMesh;

        private TransformComponent _transformComponent;
        private RigidBody3DComponent _rigidbody3DComponent;
        private AudioSource2DComponent _audioSource2DComponent;
        private Camera _mainCamera;

        // Implement OnXButtonClicked methods
        private bool _isPlayingAudio = false;

        public float JumpForce = 50f;
        public float JumpCooldownTime = 1.0f;
        private float _jumpCooldownTimer = 0.0f;

        internal Player() : base()
        {
        }

        public Player(ulong id) : base(id)
        {
        }

        protected override void OnCreate()
        {
            Console.WriteLine($"Player::OnCreate - {ID}");

            _transformComponent = GetComponent<TransformComponent>();

            if (HasComponent<RigidBody3DComponent>())
                _rigidbody3DComponent = GetComponent<RigidBody3DComponent>();

            if (HasComponent<AudioSource2DComponent>())
                _audioSource2DComponent = GetComponent<AudioSource2DComponent>();

            Entity cameraEntity = FindEntityByName("Camera");
            if (cameraEntity != null)
                _mainCamera = cameraEntity.As<Camera>();

        }

        protected override void OnUpdate(float deltaTime)
        {
            HandleShooting(deltaTime);

            Vector3 velocity = Vector3.Zero;

            _jumpCooldownTimer -= deltaTime;
            bool isGrounded = Physics.Raycast(_transformComponent.Translation, Vector3.Down, 5f, out RaycastHit hit);
            if (Input.IsKeyDown(KeyCode.Space) && isGrounded && _jumpCooldownTimer <= 0.0f)
            {
                velocity.Y += 1.0f * JumpForce;
                _jumpCooldownTimer = JumpCooldownTime;
            }

            if (Input.IsKeyDown(KeyCode.A))
                velocity.X -= 1.0f;
            if (Input.IsKeyDown(KeyCode.D))
                velocity.X += 1.0f;
            if (Input.IsKeyDown(KeyCode.W))
                velocity.Z += 1.0f;
            if (Input.IsKeyDown(KeyCode.S))
                velocity.Z -= 1.0f;
            
            if (_rigidbody3DComponent != null && Input.IsKeyDown(KeyCode.F))
            {
                _rigidbody3DComponent.ApplyImpulse(new Vector3(0.5f, 0.0f, 0.0f));
                return;
            }

            velocity *= Speed;

            ApplyVelocity(velocity, deltaTime);

            // Zoom camera in and out with Q and E
            if (_mainCamera == null) return;
            if (Input.IsKeyDown(KeyCode.Q))
                _mainCamera.DistanceFromPlayer += 1.0f * deltaTime;
            if (Input.IsKeyDown(KeyCode.E))
                _mainCamera.DistanceFromPlayer -= 1.0f * deltaTime;

            if (Input.IsKeyDown(KeyCode.P) && _audioSource2DComponent != null && !_isPlayingAudio)
            {
                _audioSource2DComponent.Play();
                _isPlayingAudio = true;
            }
            if (Input.IsKeyDown(KeyCode.O) && _audioSource2DComponent != null && _isPlayingAudio)
            {
                _audioSource2DComponent.Stop();
                _isPlayingAudio = false;
            }
        }

        protected override void OnCollisionEnter(Collision collision)
        {
            _audioSource2DComponent?.Play();
        }

        private void HandleShooting(float deltaTime)
        {
            _shootingCooldownTimer -= deltaTime;
            if (Input.IsMouseButtonDown(MouseButton.Left) && _shootingCooldownTimer <= 0.0f)
            {
                Shoot();
                _shootingCooldownTimer = ShootingCooldownTime;
            }
        }

        private void Shoot()
        {
            Entity bulletEntity = Instantiate("bullet");
            bulletEntity.AddComponent<BoxCollider3DComponent>();
            bulletEntity.AddComponent<RigidBody3DComponent>();
            bulletEntity.AddComponent<StaticMeshComponent>();
            bulletEntity.GetComponent<StaticMeshComponent>().Mesh = BulletMesh;

            Vector3 playerPos = Translation;
            Console.WriteLine($"Player position when shooting: {playerPos.X}, {playerPos.Y}, {playerPos.Z}");
            Vector3 bulletPosition = playerPos + new Vector3(0, 0, 3); // Spawn in front of the player
            Console.WriteLine($"Bullet starting position: {bulletPosition.X}, {bulletPosition.Y}, {bulletPosition.Z}");
            bulletEntity.Translation = bulletPosition;

            Vector3 bulletVelocity = new Vector3(0, 0, 100); // Shoot forward
            bulletEntity.GetComponent<RigidBody3DComponent>().Velocity = bulletVelocity;
        }

        private void ApplyVelocity(Vector3 velocity, float deltaTime)
        {
            if (_rigidbody3DComponent != null)
            {
                ApplyPhysicsVelocity(velocity);
            }
            else
            {
                ApplyVelocityWithoutPhysics(velocity, deltaTime);
            }
        }

        private void ApplyPhysicsVelocity(Vector3 velocity)
        {
            if (_rigidbody3DComponent == null)
            {
                throw new InvalidOperationException("RigidBody3DComponent is not available for physics-based movement.");
            }
            _rigidbody3DComponent.ApplyImpulse(velocity);
        }

        private void ApplyVelocityWithoutPhysics(Vector3 velocity, float deltaTime)
        {
            Vector3 translation = Translation;
            translation += velocity * deltaTime;
            Translation = translation;
        }
    }
}

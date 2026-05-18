using System;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source.Kerberos;

public class CameraFollow : Entity
{
    public string TargetName = "Player";
    public float RotationSpeed = 2.0f; // Radians per second
    public float ZoomSpeed = 5.0f;
    public float MinDistance = 2.0f;
    public float MaxDistance = 30.0f;
    public Vector2 ShoulderOffset = new Vector2(1.5f, 2.5f);
    public Vector3 TargetPivotOffset = new Vector3(0.0f, 1.5f, 0.0f);
    public bool SmoothFollow = true;

    private TransformComponent _transform;
    private Entity _target;
    private float _yaw = 0.0f;
    private float _pitch = 10.0f * (float)(Math.PI / 180.0);
    private float _distance = 0.0f;

    internal CameraFollow() : base() { }
    public CameraFollow(ulong id) : base(id) { }

    protected override void OnCreate()
    {
        _transform = GetComponent<TransformComponent>();
        _target = FindEntityByName("Dragon");

        _distance = MinDistance;
    }

    protected override void OnUpdate(float deltaTime)
    {
        if (_transform == null)
            throw new Exception("Transform cannot be null");
        if (_target == null)
            throw new Exception("Target cannot be null");

        HandleInput(deltaTime);
        ValidateAndClampMath();
        UpdateCameraTransform(deltaTime);

        //float yawInput = 0f;
        //float pitchInput = 0f;
        //if (Input.IsKeyDown(KeyCode.Left)) yawInput -= 1.0f;
        //if (Input.IsKeyDown(KeyCode.Right)) yawInput += 1.0f;
        //if (Input.IsKeyDown(KeyCode.Up)) pitchInput += 1.0f;
        //if (Input.IsKeyDown(KeyCode.Down)) pitchInput -= 1.0f;

        //// Zoom keys (you used Q/E for camera in Player; reuse or pick others)
        //if (Input.IsKeyDown(KeyCode.Q)) Distance += ZoomStep * deltaTime;
        //if (Input.IsKeyDown(KeyCode.E)) Distance -= ZoomStep * deltaTime;
        //Distance = Math.Max(MinDistance, Math.Min(MaxDistance, Distance));

        //// apply rotations
        //_yaw += yawInput * OrbitSpeed * deltaTime;
        //_pitch += pitchInput * PitchSpeed * deltaTime;
        //_pitch = Math.Max(MinPitch, Math.Min(MaxPitch, _pitch));

        //// compute desired camera offset from target using spherical coords
        //double yawRad = _yaw * Constants.Deg2Rad;
        //double pitchRad = _pitch * Constants.Deg2Rad;
        //float cosP = (float)Math.Cos(pitchRad);
        //Vector3 offset = new Vector3(
        //    (float)(Math.Sin(yawRad) * cosP) * Distance,
        //    (float)Math.Sin(pitchRad) * Distance + Height,
        //    (float)(Math.Cos(yawRad) * cosP) * Distance
        //);

        //Vector3 targetPos = _target.Translation;
        //Vector3 desiredPos = targetPos + offset;

        ////_transform.LookAt(new Vector3(desiredPos.X, desiredPos.Y + Height * 0.5f, desiredPos.Z));

        //// smooth position
        //Vector3 currentPos = _transform.Translation;
        //float t = 1.0f - (float)Math.Exp(-SmoothSpeed * deltaTime); // nicer smoothing than linear lerp factor
        //Vector3 newPos = Maths.Lerp(currentPos, desiredPos, t);
        //_transform.Translation = newPos;

        //_transform.LookAt(new Vector3(targetPos.X, targetPos.Y + Height * 0.5f, targetPos.Z));
    }

    private void HandleInput(float deltaTime)
    {
        if (Input.IsKeyDown(KeyCode.Left)) _yaw += RotationSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.Right)) _yaw -= RotationSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.Up)) _pitch += RotationSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.Down)) _pitch -= RotationSpeed * deltaTime;

        if (Input.IsKeyDown(KeyCode.Q)) _distance += ZoomSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.E)) _distance -= ZoomSpeed * deltaTime;
    }

    private void ValidateAndClampMath()
    {
        float maxPitch = 89.0f * (float)(Math.PI / 180.0);
        _distance = Math.Max(MinDistance, Math.Min(MaxDistance, _distance));
        _pitch = Math.Max(-maxPitch, Math.Min(maxPitch, _pitch));

        float twoPi = 2.0f * (float)Math.PI;
        if (_yaw > twoPi)
            _yaw = _yaw % twoPi;
        else if (_yaw < 0.0f)
            _yaw = twoPi - (Math.Abs(_yaw) % twoPi);
    }

    private void UpdateCameraTransform(float deltaTime)
    {
        Vector3 targetPos = _target.Translation;
        Vector3 pivotPosition = targetPos + TargetPivotOffset;

        Vector3 forward;
        forward.X = (float)(Math.Cos(_pitch) * -1.0 * Math.Sin(_yaw));
        forward.Y = (float)Math.Sin(_pitch);
        forward.Z = (float)(Math.Cos(_pitch) * -1.0f * Math.Cos(_yaw));

        forward.Normalize();

        Vector3 globalUp = new Vector3(0.0f, 1.0f, 0.0f);
        Vector3 right = Vector3.Cross(forward, globalUp).Normalized();

        Vector3 up = Vector3.Cross(right, forward).Normalized();

        Vector3 cameraPos = pivotPosition - (forward * _distance);
        cameraPos += right * ShoulderOffset.X;
        cameraPos += up * ShoulderOffset.Y;

        if (SmoothFollow)
        {
            Vector3 currentPos = _transform.Translation;
            const float smoothSpeed = 1.0f;
            float t = 1.0f - (float)Math.Exp(-smoothSpeed * deltaTime);
            cameraPos = Maths.Lerp(currentPos, cameraPos, t);
        }

        _transform.Translation = cameraPos;

        _transform.Rotation = new Vector3(_pitch, _yaw, 0.0f);
    }
}
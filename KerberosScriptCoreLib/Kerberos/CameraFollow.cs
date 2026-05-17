using System;
using Kerberos.Source.Kerberos.Core;
using Kerberos.Source.Kerberos.Scene;

namespace Kerberos.Source.Kerberos;

public class CameraFollow : Entity
{
    public string TargetName = "Player"; 
    public float Distance = 6.0f; 
    public float Height = 2.0f; 
    public float OrbitSpeed = 120.0f; // degrees/sec via keys
    public float PitchSpeed = 80.0f; // degrees/sec via keys
    public float SmoothSpeed = 8.0f; // lerp speed
    public float MinPitch = -20.0f;  
    public float MaxPitch = 60.0f;  
    public float ZoomStep = 1.0f;  
    public float MinDistance = 2.0f;
    public float MaxDistance = 20.0f;

    private TransformComponent _transform;
    private Entity _target;
    private float _yaw = 0.0f;   // degrees
    private float _pitch = 10.0f; // degrees

    internal CameraFollow() : base() { }
    public CameraFollow(ulong id) : base(id) { }

    protected override void OnCreate()
    {
        _transform = GetComponent<TransformComponent>();
        _target = FindEntityByName("Dragon");
        if (_target != null)
        {
            // initialize angles from current camera->target vector
            var dir = _target.Translation - _transform.Translation;
            var horiz = new Vector2(dir.X, dir.Z).Magnitude;
            if (horiz > 0.0001f)
            {
                _pitch = (float)(Math.Atan2(dir.Y, horiz) * Constants.Rad2Deg);
                _yaw = (float)(Math.Atan2(dir.X, dir.Z) * Constants.Rad2Deg);
            }
        }
    }

    protected override void OnUpdate(float deltaTime)
    {
        if (_target == null)
            _target = FindEntityByName("Dragon");
        if (_target == null) return;

        float yawInput = 0f;
        float pitchInput = 0f;
        if (Input.IsKeyDown(KeyCode.Left)) yawInput -= 1.0f;
        if (Input.IsKeyDown(KeyCode.Right)) yawInput += 1.0f;
        if (Input.IsKeyDown(KeyCode.Up)) pitchInput += 1.0f;
        if (Input.IsKeyDown(KeyCode.Down)) pitchInput -= 1.0f;

        // Zoom keys (you used Q/E for camera in Player; reuse or pick others)
        if (Input.IsKeyDown(KeyCode.Q)) Distance += ZoomStep * deltaTime;
        if (Input.IsKeyDown(KeyCode.E)) Distance -= ZoomStep * deltaTime;
        Distance = Math.Max(MinDistance, Math.Min(MaxDistance, Distance));

        // apply rotations
        _yaw += yawInput * OrbitSpeed * deltaTime;
        _pitch += pitchInput * PitchSpeed * deltaTime;
        _pitch = Math.Max(MinPitch, Math.Min(MaxPitch, _pitch));

        // compute desired camera offset from target using spherical coords
        double yawRad = _yaw * Constants.Deg2Rad;
        double pitchRad = _pitch * Constants.Deg2Rad;
        float cosP = (float)Math.Cos(pitchRad);
        Vector3 offset = new Vector3(
            (float)(Math.Sin(yawRad) * cosP) * Distance,
            (float)Math.Sin(pitchRad) * Distance + Height,
            (float)(Math.Cos(yawRad) * cosP) * Distance
        );

        Vector3 targetPos = _target.Translation;
        Vector3 desiredPos = targetPos + offset;

        //_transform.LookAt(new Vector3(desiredPos.X, desiredPos.Y + Height * 0.5f, desiredPos.Z));

        // smooth position
        Vector3 currentPos = _transform.Translation;
        float t = 1.0f - (float)Math.Exp(-SmoothSpeed * deltaTime); // nicer smoothing than linear lerp factor
        Vector3 newPos = Maths.Lerp(currentPos, desiredPos, t);
        _transform.Translation = newPos;

        _transform.LookAt(new Vector3(targetPos.X, targetPos.Y + Height * 0.5f, targetPos.Z));
    }
}
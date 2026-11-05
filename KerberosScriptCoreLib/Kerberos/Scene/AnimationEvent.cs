namespace Kerberos.Scene
{
    public enum AnimationEventPhase
    {
        Point = 0,
        Begin,
        Tick,
        End
    }

    public readonly struct AnimationEvent
    {
        public AnimationEvent(string name,
                              string payload,
                              string clipName,
                              float time,
                              float duration,
                              float weight,
                              AnimationEventPhase phase,
                              uint flags)
        {
            Name = name;
            Payload = payload;
            ClipName = clipName;
            Time = time;
            Duration = duration;
            Weight = weight;
            Phase = phase;
            Flags = flags;
        }

        public string Name { get; }
        public string Payload { get; }
        public string ClipName { get; }
        public float Time { get; }
        public float Duration { get; }
        public float Weight { get; }
        public AnimationEventPhase Phase { get; }
        public uint Flags { get; }
    }
}

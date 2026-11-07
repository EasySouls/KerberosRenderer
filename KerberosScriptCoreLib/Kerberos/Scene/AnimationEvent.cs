using System.Collections.Generic;
using Kerberos.Core;

namespace Kerberos.Scene
{
    public enum AnimationEventPhase
    {
        Point = 0,
        Begin,
        Tick,
        End
    }

    /// <summary>
    /// Type of a named animation-event parameter. Games decide what the names and values mean;
    /// the engine only stores and forwards them.
    /// </summary>
    public enum AnimationEventParameterType
    {
        Bool = 0,
        Int,
        Float,
        Vector3,
        String,
        Asset
    }

    public readonly struct AnimationEventParameter
    {
        public AnimationEventParameter(string name,
                                       AnimationEventParameterType type,
                                       bool boolValue,
                                       int intValue,
                                       float floatValue,
                                       Vector3 vectorValue,
                                       string stringValue,
                                       ulong assetValue)
        {
            Name = name;
            Type = type;
            BoolValue = boolValue;
            IntValue = intValue;
            FloatValue = floatValue;
            VectorValue = vectorValue;
            StringValue = stringValue;
            AssetValue = assetValue;
        }

        public string Name { get; }
        public AnimationEventParameterType Type { get; }
        public bool BoolValue { get; }
        public int IntValue { get; }
        public float FloatValue { get; }
        public Vector3 VectorValue { get; }
        public string StringValue { get; }
        public ulong AssetValue { get; }
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
                              uint flags,
                              IReadOnlyList<AnimationEventParameter> parameters)
        {
            Name = name;
            Payload = payload;
            ClipName = clipName;
            Time = time;
            Duration = duration;
            Weight = weight;
            Phase = phase;
            Flags = flags;
            Parameters = parameters;
        }

        public string Name { get; }
        public string Payload { get; }
        public string ClipName { get; }
        public float Time { get; }
        public float Duration { get; }
        public float Weight { get; }
        public AnimationEventPhase Phase { get; }
        public uint Flags { get; }
        public IReadOnlyList<AnimationEventParameter> Parameters { get; }

        public bool TryGetParameter(string name, out AnimationEventParameter parameter)
        {
            for (int i = 0; i < Parameters.Count; ++i)
            {
                if (Parameters[i].Name == name)
                {
                    parameter = Parameters[i];
                    return true;
                }
            }

            parameter = default;
            return false;
        }

        public AnimationEventParameter? FindParameter(string name)
            => TryGetParameter(name, out AnimationEventParameter parameter) ? parameter : null;

        public bool GetBool(string name, bool fallback = false)
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.Bool
                ? parameter.BoolValue
                : fallback;

        public int GetInt(string name, int fallback = 0)
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.Int
                ? parameter.IntValue
                : fallback;

        public float GetFloat(string name, float fallback = 0.0f)
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.Float
                ? parameter.FloatValue
                : fallback;

        public Vector3 GetVector3(string name, Vector3 fallback = default)
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.Vector3
                ? parameter.VectorValue
                : fallback;

        public string GetString(string name, string fallback = "")
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.String
                ? parameter.StringValue
                : fallback;

        public ulong GetAsset(string name, ulong fallback = 0)
            => TryGetParameter(name, out AnimationEventParameter parameter) && parameter.Type == AnimationEventParameterType.Asset
                ? parameter.AssetValue
                : fallback;
    }
}

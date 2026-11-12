namespace Kerberos.AI;

using System.Text.Json;

public static class AI
{
    public static bool RegisterStateTree(string name, AIStateTreeDoc doc)
        => InternalCalls.AI_RegisterStateTree(name, JsonSerializer.Serialize(doc));

    public static bool RegisterStateTree(string name, string json)
        => InternalCalls.AI_RegisterStateTree(name, json);

    public static bool UnregisterStateTree(string name)
        => InternalCalls.AI_UnregisterStateTree(name);

    public static bool StateTreeExists(string name)
        => InternalCalls.AI_StateTreeExists(name);
}
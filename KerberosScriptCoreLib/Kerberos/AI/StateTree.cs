namespace Kerberos.AI;

public sealed class AIStateTreeDoc
{
    public string Entry { get; set; } = "";
    public List<AIStateDoc> States { get; set; } = new();
}

public sealed class AIStateDoc
{
    public string Name { get; set; } = "";
    public List<AINodeDoc> Tasks { get; set; } = new();
    public List<AITransitionDoc> Transitions { get; set; } = new();
}

public sealed class AINodeDoc
{
    public string Type { get; set; } = "";
    public Dictionary<string, object?> Params { get; set; } = new();
}

public sealed class AITransitionDoc
{
    public string To { get; set; } = "";
    public string? Name { get; set; }
    public List<AINodeDoc> When { get; set; } = new();
}
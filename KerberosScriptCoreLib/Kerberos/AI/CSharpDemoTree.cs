using System;
using System.Collections.Generic;
using Kerberos.Scene;
using Kerberos.AI;

using AIApi = Kerberos.AI.AI;

namespace Kerberos
{
    /// <summary>
    /// Registers a view-only demo state tree from C#. Attach this script to any
    /// entity and enter Play; "CSharpDemo" then appears in the registered-tree
    /// picker of View > AI Editor. Script-owned trees are cleared on project
    /// open and script reload, so registration happens in OnCreate.
    /// </summary>
    public class CSharpDemoTree : Entity
    {
        public const string TreeName = "CSharpDemo";

        internal CSharpDemoTree() : base()
        {
        }

        public CSharpDemoTree(ulong id) : base(id)
        {
        }

        protected override void OnCreate()
        {
            if (!AIApi.RegisterStateTree(TreeName, BuildDocument()))
            {
                Console.WriteLine($"CSharpDemoTree failed to register '{TreeName}'.");
                return;
            }

            // Convenience for testing: when the script sits on an entity that
            // already has an AI agent, bind the newly registered tree. This runs
            // before AISystem.OnStart, so the agent resolves it on the first tick.
            if (HasComponent<AIStateMachineComponent>())
            {
                AIStateMachineComponent stateMachine = GetComponent<AIStateMachineComponent>();
                if (string.IsNullOrEmpty(stateMachine.TreeName))
                    stateMachine.TreeName = TreeName;
            }
        }

        private static AIStateTreeDoc BuildDocument()
        {
            return new AIStateTreeDoc
            {
                Entry = "Idle",
                States = new List<AIStateDoc>
                {
                    new()
                    {
                        Name = "Idle",
                        Tasks = new List<AINodeDoc>
                        {
                            Node("Stop"),
                            Node("Wait", ("Duration", 1.0f)),
                        },
                        Transitions = new List<AITransitionDoc>
                        {
                            Transition("Chase", "PlayerSpotted",
                                Node("TargetExists", ("TargetKey", "TargetEntity")),
                                Node("DistanceToTarget", ("TargetKey", "TargetEntity"),
                                    ("Distance", 12.0f), ("LessThan", true))),
                            Transition("Search", "PatrolTimeout",
                                Node("TimeInState", ("Time", 5.0f), ("LessThan", false))),
                        },
                    },
                    new()
                    {
                        Name = "Search",
                        Tasks = new List<AINodeDoc>
                        {
                            Node("SetBlackboardBool", ("Key", "HasLineOfSight"), ("Value", false)),
                            Node("Wait", ("Duration", 0.5f)),
                        },
                        Transitions = new List<AITransitionDoc>
                        {
                            Transition("Chase", "PlayerVisible",
                                Node("TargetVisible")),
                            Transition("Idle", "SearchExpired",
                                Node("TimeInState", ("Time", 4.0f), ("LessThan", false))),
                        },
                    },
                    new()
                    {
                        Name = "Chase",
                        Tasks = new List<AINodeDoc>
                        {
                            Node("ChaseTarget", ("TargetKey", "TargetEntity"), ("Speed", 4.5f)),
                        },
                        Transitions = new List<AITransitionDoc>
                        {
                            Transition("Attack", "InAttackRange",
                                Node("TargetVisible"),
                                Node("DistanceToTarget", ("TargetKey", "TargetEntity"),
                                    ("Distance", 1.8f), ("LessThan", true))),
                            Transition("Search", "LostSight",
                                Node("BlackboardBool", ("Key", "HasLineOfSight"), ("Value", false))),
                            Transition("Flee", "LowHealth",
                                Node("BlackboardBool", ("Key", "LowHealth"), ("Value", true))),
                        },
                    },
                    new()
                    {
                        Name = "Attack",
                        Tasks = new List<AINodeDoc>
                        {
                            Node("FaceTarget", ("TargetKey", "TargetEntity")),
                            Node("AttackTarget"),
                        },
                        Transitions = new List<AITransitionDoc>
                        {
                            Transition("Chase", "TargetEscaped",
                                Node("DistanceToTarget", ("TargetKey", "TargetEntity"),
                                    ("Distance", 2.2f), ("LessThan", false))),
                            Transition("Search", "TargetLost",
                                Node("BlackboardBool", ("Key", "HasLineOfSight"), ("Value", false))),
                        },
                    },
                    new()
                    {
                        Name = "Flee",
                        Tasks = new List<AINodeDoc>
                        {
                            Node("SetBlackboardBool", ("Key", "LowHealth"), ("Value", false)),
                            Node("Wait", ("Duration", 1.0f)),
                        },
                        Transitions = new List<AITransitionDoc>
                        {
                            Transition("Idle", "Regrouped",
                                Node("TimeInState", ("Time", 3.0f), ("LessThan", false))),
                        },
                    },
                },
            };
        }

        private static AINodeDoc Node(string type, params (string Key, object? Value)[] parameters)
        {
            var node = new AINodeDoc { Type = type };
            foreach ((string key, object? value) in parameters)
                node.Params[key] = value;
            return node;
        }

        private static AITransitionDoc Transition(string to, string name, params AINodeDoc[] conditions)
        {
            return new AITransitionDoc
            {
                To = to,
                Name = name,
                When = new List<AINodeDoc>(conditions),
            };
        }
    }
}

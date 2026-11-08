#pragma once 

#include "Scene/Components/AIComponents.hpp"

namespace Kerberos {

class Scene;
class Entity;

class AISystem
{
public:
    explicit AISystem(Scene& scene);

    //Clear runtime state, seed authored blackboards, build task instances.
    void OnStart(); 
    // Clear runtime instances and values
    void OnStop();

    void Update(float dt) const;
    bool IsEnabled() const;
    void SetEnabled(bool enabled);

private:
    void UpdatePerception(Entity entity, AIBlackboardComponent&, AIPerceptionComponent&, float dt);
    void UpdateStateMachine(Entity entity, AIBlackboardComponent&, AIStateMachineComponent&, float dt);
    void UpdateMovement(Entity entity, AIStateMachineComponent&, AIControllerComponent&, float dt);

    Scene* m_Scene = nullptr;
    bool m_Enabled = true;
};

}
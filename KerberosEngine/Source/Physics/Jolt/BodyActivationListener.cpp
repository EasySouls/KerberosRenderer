#include "BodyActivationListener.hpp"

import Kerberos;

namespace Kerberos::Physics {

void BodyActivationListener::OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData)
{
    Log::CoreTrace("A body got activated: {}", inBodyID.GetIndex());
}
void BodyActivationListener::OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData)
{
    Log::CoreTrace("A body went to sleep: {}", inBodyID.GetIndex());
}

}
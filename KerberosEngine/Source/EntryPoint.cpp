#include "EntryPoint.hpp"

#include "Profiling/Instrumentor.hpp"
#include "Logging/CrashHandler.hpp"

import Kerberos;

int main(const int argc, char** argv)
{
    Kerberos::Log::Init();
    Kerberos::CrashHandler::Init();

    KBR_PROFILE_BEGIN_SESSION("Startup", "KerberosRenderer_StartupProfile.json");
    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto app = Kerberos::CreateApplication({ .Count = argc, .Args = argv });
    const auto endTime = std::chrono::high_resolution_clock::now();
    KBR_PROFILE_END_SESSION();

    Kerberos::Log::CoreInfo("Application startup took {:.2f} ms",
                            std::chrono::duration<float, std::milli>(endTime - startTime).count());

    KBR_PROFILE_BEGIN_SESSION("Runtime", "KerberosRenderer_RuntimeProfile.json");
    app->Run();
    KBR_PROFILE_END_SESSION();

    KBR_PROFILE_BEGIN_SESSION("Shutdown", "KerberosRenderer_ShutdownProfile.json");
    delete app;

    Kerberos::Log::Shutdown();
    KBR_PROFILE_END_SESSION();
}
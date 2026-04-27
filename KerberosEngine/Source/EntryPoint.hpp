#pragma once

#include "Application.hpp"
#include "Debug/Instrumentor.hpp"
#include "Logging/CrashHandler.hpp"

namespace Kerberos
{
	/*
	* To be defined in client
	*/
	extern Application* CreateApplication(ApplicationCommandLineArgs args);
}

int main(const int argc, char** argv)
{
	Kerberos::Log::Init();
	Kerberos::CrashHandler::Init();

	KBR_PROFILE_BEGIN_SESSION("Startup", "KerberosRenderer_StartupProfile.json");
	const auto app = Kerberos::CreateApplication({ .Count = argc, .Args = argv });
	KBR_PROFILE_END_SESSION();

	KBR_PROFILE_BEGIN_SESSION("Runtime", "KerberosRenderer_RuntimeProfile.json");
	app->Run();
	KBR_PROFILE_END_SESSION();

	KBR_PROFILE_BEGIN_SESSION("Shutdown", "KerberosRenderer_ShutdownProfile.json");
	delete app;

	Kerberos::Log::Shutdown();
	KBR_PROFILE_END_SESSION();
}
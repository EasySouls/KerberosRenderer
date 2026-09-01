#pragma once

#include "Application.hpp"

namespace Kerberos
{
	/*
	* To be defined in client
	*/
	extern Application* CreateApplication(ApplicationCommandLineArgs args);
}

int main(int argc, char** argv);
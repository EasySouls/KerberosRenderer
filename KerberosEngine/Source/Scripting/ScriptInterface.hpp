#pragma once

namespace Kerberos
{

    class ScriptInterface
    {
    public:
        /// Registers native function pointers with the managed scripting system.
        static void RegisterFunctions();
    };

}
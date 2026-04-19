import os
import sys
import shutil
from pathlib import Path

class DotNetConfiguration:
    requiredDotNetVersion = "10.0"
    vendorLibDir = "./KerberosEngine/ThirdParty/dotnet/lib"

    @classmethod
    def Validate(cls):
        if not cls.CheckDotNetSDK():
            print(f"  .NET SDK not found. Please install .NET {cls.requiredDotNetVersion} SDK from https://dotnet.microsoft.com/download")
            return False

        if not cls.CheckNetHostLib():
            print(f"  nethost library not found in ThirdParty directory. Attempting to copy from .NET {cls.requiredDotNetVersion} SDK...")
            return cls.CopyNetHostLib()

        print(f"  nethost library already set up correctly for .NET {cls.requiredDotNetVersion}.")
        return True

    @classmethod
    def CheckDotNetSDK(cls):
        dotnetRoot = cls.GetDotNetRoot()
        if dotnetRoot is None:
            return False
        print(f"\nLocated .NET SDK at {dotnetRoot}")
        return True

    @classmethod
    def GetDotNetRoot(cls):
        # Check DOTNET_ROOT environment variable first
        dotnetRoot = os.environ.get("DOTNET_ROOT")
        if dotnetRoot and os.path.isdir(dotnetRoot):
            return dotnetRoot

        # Check common install locations
        if sys.platform == "win32":
            programFiles = os.environ.get("ProgramFiles", r"C:\Program Files")
            defaultPath = os.path.join(programFiles, "dotnet")
            if os.path.isdir(defaultPath):
                return defaultPath
        else:
            for path in ["/usr/share/dotnet", "/usr/lib/dotnet", os.path.expanduser("~/.dotnet")]:
                if os.path.isdir(path):
                    return path

        return None

    @classmethod
    def GetRuntimeId(cls):
        if sys.platform == "win32":
            return "win-x64"
        elif sys.platform == "linux":
            return "linux-x64"
        elif sys.platform == "darwin":
            return "osx-x64"
        return None

    @classmethod
    def GetNetHostSourceDir(cls):
        """Find the directory containing nethost.lib/libnethost.a in the .NET SDK host packs."""
        dotnetRoot = cls.GetDotNetRoot()
        if dotnetRoot is None:
            return None

        rid = cls.GetRuntimeId()
        if rid is None:
            return None

        hostPackDir = os.path.join(dotnetRoot, "packs", f"Microsoft.NETCore.App.Host.{rid}")
        if not os.path.isdir(hostPackDir):
            return None

        # Find the latest requiredDotNetVersion.x version
        versions = [d for d in os.listdir(hostPackDir) if d.startswith(cls.requiredDotNetVersion)]
        if not versions:
            return None

        versions.sort(key=lambda v: [int(x) for x in v.split('.') if x.isdigit()], reverse=True)
        latestVersion = versions[0]

        nativeDir = os.path.join(hostPackDir, latestVersion, "runtimes", rid, "native")
        if os.path.isdir(nativeDir):
            return nativeDir

        return None

    @classmethod
    def CheckNetHostLib(cls):
        if sys.platform == "win32":
            libName = "nethost.lib"
        else:
            libName = "libnethost.a"

        debugLib = os.path.join(cls.vendorLibDir, "Debug", libName)
        releaseLib = os.path.join(cls.vendorLibDir, "Release", libName)
        return os.path.isfile(debugLib) and os.path.isfile(releaseLib)

    @classmethod
    def CopyNetHostLib(cls):
        sourceDir = cls.GetNetHostSourceDir()
        if sourceDir is None:
            print(f"  Could not find nethost library in .NET SDK.")
            print(f"  Please install the .NET {cls.requiredDotNetVersion} SDK from https://dotnet.microsoft.com/download")
            return False

        print(f"  Found nethost native directory at: {sourceDir}")

        # Determine which files to copy
        filesToCopy = []
        if sys.platform == "win32":
            filesToCopy = ["nethost.lib", "nethost.dll"]
        else:
            filesToCopy = ["libnethost.a", "libnethost.so"]

        for config in ["Debug", "Release"]:
            destDir = os.path.join(cls.vendorLibDir, config)
            os.makedirs(destDir, exist_ok=True)
            for fileName in filesToCopy:
                sourceFile = os.path.join(sourceDir, fileName)
                if os.path.isfile(sourceFile):
                    destFile = os.path.join(destDir, fileName)
                    shutil.copy2(sourceFile, destFile)
                    print(f"  Copied {fileName} to {destDir}")

        print("  .NET host library setup complete.")
        return True

if __name__ == "__main__":
    DotNetConfiguration.Validate()

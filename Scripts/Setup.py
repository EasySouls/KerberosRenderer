
import os
import subprocess
import platform

from SetupPython import PythonConfiguration as PythonRequirements
from SetupDotNet import DotNetConfiguration as DotNetRequirements
#from SetupVulkan import VulkanConfiguration as VulkanRequirements

# Make sure everything we need for the setup is installed
PythonRequirements.Validate()

os.chdir('./../') # Change from devtools/scripts directory to root

# If we are in CI we do not need to validate the VulkanSDK
#if os.getenv('CI') is None:
#   VulkanRequirements.Validate()

DotNetRequirements.Validate()

print("\nUpdating submodules...")
subprocess.call(["git", "submodule", "update", "--init", "--recursive"])

print("\nSetup completed!")
#include "pch.hpp"
#include "SlangCompiler.hpp"

#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

#include "IO.hpp"
#include "Logging/Log.hpp"

namespace kbr
{
	static Slang::ComPtr<slang::IGlobalSession> GetGlobalSession()
	{
		static Slang::ComPtr<slang::IGlobalSession> globalSession;
		if (!globalSession)
		{
			constexpr SlangGlobalSessionDesc desc = {};
			slang::createGlobalSession(&desc, globalSession.writeRef());
		}
		return globalSession;
	}

	std::vector<uint32_t> SlangCompiler::CompileToSpirv(const std::filesystem::path& filepath, const std::vector<ShaderEntryPoint>& entryPoints)
	{
		using namespace slang;

		const Slang::ComPtr<IGlobalSession> globalSession = GetGlobalSession();

		SessionDesc sessionDesc = {};

		const SlangProfileID profile = globalSession->findProfile("spirv_1_6");
		if (profile == SLANG_PROFILE_UNKNOWN)
		{
			KBR_CORE_ERROR("Failed to find Slang profile 'spirv_1_6'");
			throw CompilationFailedException("Unknown Slang profile");
		}

		const TargetDesc targetDesc{
			.format = SLANG_SPIRV,
			.profile = profile,
		};
		sessionDesc.targetCount = 1;
		sessionDesc.targets = &targetDesc;

		const char* searchPaths[] = { "assets/shaders" };
		sessionDesc.searchPathCount = 1;
		sessionDesc.searchPaths = searchPaths;

		std::vector<CompilerOptionEntry> compilerOptions;
		compilerOptions.push_back({ .name = CompilerOptionName::EmitSpirvDirectly, .value = { .intValue0 = 1 } });
		compilerOptions.push_back({ .name = CompilerOptionName::VulkanUseEntryPointName, .value = { .intValue0 = 1 } });
#ifdef KBR_DEBUG
		compilerOptions.push_back({ .name = CompilerOptionName::VulkanEmitReflection, .value = { .intValue0 = 1 } });
		//compilerOptions.push_back({ .name = CompilerOptionName::DebugInformation, .value = { .intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL } });
#endif
		/*compilerOptions.push_back({ .name = CompilerOptionName::Capability, .value = {
			.kind = CompilerOptionValueKind::String,
			.stringValue0 = "vk_mem_model"
		} });*/
		sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());
		sessionDesc.compilerOptionEntries = compilerOptions.data();

		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

		Slang::ComPtr<ISession> session;
		if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef())))
		{
			KBR_CORE_ERROR("Failed to create Slang session");
			throw CompilationFailedException("Failed to create Slang session");
		}

		const std::string moduleName = filepath.stem().string();
		Slang::ComPtr<IBlob> moduleDiagnostics;
		/*Slang::ComPtr<IModule> module = session->loadModuleFromSource(
			filepath.filename().string().c_str(),
			filepath.string().c_str(),
			IO::ReadFileBlob(filepath),
			diagnostics.writeRef());*/
		const Slang::ComPtr<IModule> module(session->loadModule(moduleName.c_str(), moduleDiagnostics.writeRef()));
		if (moduleDiagnostics || !module)
		{
			KBR_CORE_ERROR("Shader compilation error: {}", static_cast<const char*>(moduleDiagnostics->getBufferPointer()));
			throw CompilationFailedException("Shader compilation error: " + std::string(static_cast<const char*>(moduleDiagnostics->getBufferPointer())));
		}

		std::vector<IComponentType*> entryPointComponents;
		std::vector<Slang::ComPtr<IEntryPoint>> entryPointOwners; // keeps COM objects alive

		if (entryPoints.empty())
		{
			const int32_t definedEntryPointCount = module->getDefinedEntryPointCount();
			entryPointComponents.reserve(definedEntryPointCount + 1);
			entryPointOwners.reserve(definedEntryPointCount);
			entryPointComponents.push_back(module);

			for (int32_t i = 0; i < definedEntryPointCount; ++i)
			{
				Slang::ComPtr<IEntryPoint> entryPointComponent;
				if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPointComponent.writeRef())))
				{
					KBR_CORE_ERROR("Failed to find defined entry point '{}' in shader module '{}'", i, moduleName);
					continue;
				}

				entryPointComponents.push_back(entryPointComponent);
				entryPointOwners.push_back(std::move(entryPointComponent));
			}
		}
		else
		{
			// Load desired entry points
			entryPointComponents.reserve(entryPoints.size() + 1);
			entryPointOwners.reserve(entryPoints.size());
			entryPointComponents.push_back(module);

			for (const ShaderEntryPoint& entryPoint : entryPoints)
			{
				const char* entryPointName = GetEntryPointName(entryPoint);

				Slang::ComPtr<IEntryPoint> entryPointComponent;
				if (SLANG_FAILED(module->findEntryPointByName(entryPointName, entryPointComponent.writeRef())))
				{
					KBR_CORE_ERROR("Failed to find entry point '{}' in shader module '{}'", entryPointName, moduleName);
					continue;
				}

				entryPointComponents.push_back(entryPointComponent);
				entryPointOwners.push_back(std::move(entryPointComponent));
			}
		}

		Slang::ComPtr<IComponentType> program;
		Slang::ComPtr<ISlangBlob> compositionDiagnostics;
		if (SLANG_FAILED(session->createCompositeComponentType(entryPointComponents.data(),
											  static_cast<SlangInt>(entryPointComponents.size()), 
											  program.writeRef(),
															   compositionDiagnostics.writeRef())))
		{
			KBR_CORE_ERROR("Failed to create composite component type: {}", static_cast<const char*>(compositionDiagnostics->getBufferPointer()));
			throw CompilationFailedException("Failed to create composite component type: " + std::string(static_cast<const char*>(compositionDiagnostics->getBufferPointer())));
		}
		if (compositionDiagnostics)
		{
			KBR_CORE_WARN("Shader composition warning: {}", static_cast<const char*>(compositionDiagnostics->getBufferPointer()));
		}

		// Useful if I want to do reflection later
		//ProgramLayout* layout = program->getLayout();

		Slang::ComPtr<IComponentType> linkedProgram;
		Slang::ComPtr<ISlangBlob> linkDiagnostics;
		program->linkWithOptions(linkedProgram.writeRef(), static_cast<uint32_t>(compilerOptions.size()), compilerOptions.data(), linkDiagnostics.writeRef());
		if (linkDiagnostics)
		{
			KBR_CORE_ERROR("Shader linking error: {}", static_cast<const char*>(linkDiagnostics->getBufferPointer()));
			throw CompilationFailedException("Shader linking error: " + std::string(static_cast<const char*>(linkDiagnostics->getBufferPointer())));
		}

		std::vector<uint32_t> spirvCode;

		// Example for later if I want to use other APIs like D3D12
		/*for (SlangUInt entryPointIndex = 0; entryPointIndex < layout->getEntryPointCount(); ++entryPointIndex)
		{
			Slang::ComPtr<ISlangBlob> codeBlob;
			if (SLANG_FAILED(linkedProgram->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), diagnostics.writeRef())))
			{
				KBR_CORE_ERROR("Failed to get SPIR-V code for entry point '{}': {}", layout->getEntryPointByIndex(entryPointIndex)->getName(), static_cast<const char*>(diagnostics->getBufferPointer()));
				throw CompilationFailedException("Failed to get SPIR-V code for entry point '" + std::string(layout->getEntryPointByIndex(entryPointIndex)->getName()) + "': " + std::string(static_cast<const char*>(diagnostics->getBufferPointer())));
			}
			const uint32_t* codeData = static_cast<const uint32_t*>(codeBlob->getBufferPointer());
			const size_t codeSize = codeBlob->getBufferSize() / sizeof(uint32_t);
			spirvCode.insert(spirvCode.end(), codeData, codeData + codeSize);
		}*/

		Slang::ComPtr<IBlob> spirvBlob;
		{
			constexpr SlangInt targetIndex = 0;
			Slang::ComPtr<IBlob> diagnosticsBlob;
			const SlangResult result = linkedProgram->getTargetCode(
				targetIndex,
				spirvBlob.writeRef(),
				diagnosticsBlob.writeRef());
			
			if (SLANG_FAILED(result))
			{
				KBR_CORE_ERROR("Failed to get SPIR-V code for shader '{}': {}", moduleName, static_cast<const char*>(diagnosticsBlob->getBufferPointer()));
				throw CompilationFailedException("Failed to get SPIR-V code for shader '" + moduleName + "': " + std::string(static_cast<const char*>(diagnosticsBlob->getBufferPointer())));
			}
			if (diagnosticsBlob)
			{
				KBR_CORE_WARN("Shader compilation warning for '{}': {}", moduleName, static_cast<const char*>(diagnosticsBlob->getBufferPointer()));
			}
		}

		const uint32_t* codeData = static_cast<const uint32_t*>(spirvBlob->getBufferPointer());
		const size_t codeSize = spirvBlob->getBufferSize() / sizeof(uint32_t);
		spirvCode.resize(codeSize);
		std::memcpy(spirvCode.data(), codeData, codeSize * sizeof(uint32_t));
		//spirvCode.insert(spirvCode.end(), codeData, codeData + codeSize);

		return spirvCode;
	}
}

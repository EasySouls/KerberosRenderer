#include "kbrpch.hpp"
#include "ScriptEngine.hpp"

#include "Scripting/ScriptInterface.hpp"
#include "Scripting/ScriptClass.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scripting/ScriptUtils.hpp"
//#include "Core/Filesystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Application.hpp"
#include "Project/Project.hpp"
#include "Core/Timer.hpp"

#include <dotnet/include/nethost.h>
#include <dotnet/include/hostfxr.h>
#include <dotnet/include/coreclr_delegates.h>

#include <filewatch/FileWatch.hpp>

#include <string_view>


#ifdef _WIN32
#include <Windows.h>
#define STR(s) L ## s
#define CH(c) L ## c
#define DOTNET_STR(s) L ## s
using dotnet_string = std::wstring;
#else
#define STR(s) s
#define CH(c) c
#define DOTNET_STR(s) s
using dotnet_string = std::string;
#endif

using namespace std::literals;

namespace Kerberos
{
	struct ScriptEngineData
	{
		/// .NET hosting state
		hostfxr_handle HostContext = nullptr;
		hostfxr_initialize_for_runtime_config_fn InitForConfigFn = nullptr;
		hostfxr_get_runtime_delegate_fn GetDelegateFn = nullptr;
		hostfxr_close_fn CloseFn = nullptr;
		load_assembly_and_get_function_pointer_fn LoadAssemblyFn = nullptr;

		std::filesystem::path CoreAssemblyPath;
		std::filesystem::path RuntimeConfigPath;

		ScriptClass EntityClass;

		std::unordered_map<std::string, Ref<ScriptClass>> EntityClasses;

		using FieldInitializerMap = std::unordered_map<std::string, ScriptFieldInitializer>;
		/// Holds the data for the initial values of the fields of each entity
		std::unordered_map<UUID, FieldInitializerMap> EntityFieldInitializers;

		/// Runtime data
		std::weak_ptr<Scene> SceneContext;
		std::unordered_map<UUID, Ref<ScriptInstance>> EntityInstances;

		/// Managed function pointers (from ScriptGlue bridge)
		ManagedLoadAssemblyClassesFn LoadAssemblyClasses = nullptr;
		ManagedClassExistsFn ManagedClassExists = nullptr;
		ManagedCreateInstanceFn ManagedCreateInstance = nullptr;
		ManagedDestroyInstanceFn ManagedDestroyInstance = nullptr;
		ManagedClearInstancesFn ManagedClearInstances = nullptr;
		ManagedInvokeOnCreateFn ManagedInvokeOnCreate = nullptr;
		ManagedInvokeOnUpdateFn ManagedInvokeOnUpdate = nullptr;
		ManagedInvokeOnCollisionEnterFn ManagedInvokeOnCollisionEnter = nullptr;
		ManagedInvokeOnCollisionPersistFn ManagedInvokeOnCollisionPersist = nullptr;
		ManagedInvokeOnCollisionExitFn ManagedInvokeOnCollisionExit = nullptr;
		ManagedGetFieldCountFn ManagedGetFieldCount = nullptr;
		ManagedGetFieldsFn ManagedGetFields = nullptr;
		ManagedGetFieldValueFn ManagedGetFieldValue = nullptr;
		ManagedSetFieldValueFn ManagedSetFieldValue = nullptr;
		ManagedGetEntityClassCountFn ManagedGetEntityClassCount = nullptr;
		ManagedGetEntityClassNamesFn ManagedGetEntityClassNames = nullptr;
		ManagedSetNativeCallbacksFn ManagedSetNativeCallbacks = nullptr;
	};

	static ScriptEngineData* s_ScriptData = nullptr;
	static Owner<filewatch::FileWatch<std::string>> s_Filewatcher = nullptr;

	// ====================================================================
	// Helpers for loading hostfxr
	// ====================================================================

#ifdef _WIN32
	static void* LoadLibraryHelper(const char_t* path)
	{
		const HMODULE h = ::LoadLibraryW(path);
		return static_cast<void*>(h);
	}

	template<typename T>
	static T GetExportHelper(void* lib, const char* name)
	{
		return reinterpret_cast<T>(::GetProcAddress(static_cast<HMODULE>(lib), name));
	}
#else
#include <dlfcn.h>
	static void* LoadLibraryHelper(const char_t* path)
	{
		return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
	}

	template<typename T>
	static T GetExportHelper(void* lib, const char* name)
	{
		return reinterpret_cast<T>(dlsym(lib, name));
	}
#endif

	static bool LoadHostFxr()
	{
		/// Get the path to the hostfxr library
		char_t buffer[4096];
		size_t bufferSize = sizeof(buffer) / sizeof(char_t);
		int rc = get_hostfxr_path(buffer, &bufferSize, nullptr);
		if (rc != 0)
		{
			KBR_CORE_ERROR("Failed to find hostfxr library. Ensure .NET SDK is installed. Error: 0x{:x}", rc);
			return false;
		}

		/// Load hostfxr and get the exported functions
		void* lib = LoadLibraryHelper(buffer);
		if (!lib)
		{
			KBR_CORE_ERROR("Failed to load hostfxr library");
			return false;
		}

		s_ScriptData->InitForConfigFn = GetExportHelper<hostfxr_initialize_for_runtime_config_fn>(lib, "hostfxr_initialize_for_runtime_config");
		s_ScriptData->GetDelegateFn = GetExportHelper<hostfxr_get_runtime_delegate_fn>(lib, "hostfxr_get_runtime_delegate");
		s_ScriptData->CloseFn = GetExportHelper<hostfxr_close_fn>(lib, "hostfxr_close");

		return s_ScriptData->InitForConfigFn && s_ScriptData->GetDelegateFn && s_ScriptData->CloseFn;
	}

	static dotnet_string ToDotNetString(const std::string& str)
	{
#ifdef _WIN32
		if (str.empty()) return {};
		const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
		dotnet_string wstr(sizeNeeded, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], sizeNeeded);
		return wstr;
#else
		return str;
#endif
	}

	// ====================================================================
	// ScriptEngine Implementation
	// ====================================================================

	void ScriptEngine::Init()
	{
		s_ScriptData = new ScriptEngineData();

		InitDotNet();

		const std::filesystem::path assemblyPath = std::filesystem::current_path() / "Resources"sv / "Scripts"sv / "KerberosScriptCoreLib.dll"sv;
		LoadAssembly(assemblyPath);
		LoadAssemblyClasses();

		ScriptInterface::RegisterFunctions();

		/// Setup filewatcher to reload assembly on changes
		const std::filesystem::path watchPath = std::filesystem::current_path() / "Resources"sv / "Scripts"sv;
		s_Filewatcher = CreateOwner<filewatch::FileWatch<std::string>>(
			watchPath.string(),
			OnAssemblyFileChanged
		);
	}

	void ScriptEngine::Shutdown()
	{
		ShutdownDotNet();

		delete s_ScriptData;
		s_ScriptData = nullptr;
	}

	void ScriptEngine::ReloadAssembly()
	{
		Timer reloadAssemblyTimer("Reload Assembly", [&](const TimerData& data)
		{
			KBR_CORE_INFO("Reloading C# assemblies took {:.2f} ms", data.DurationMs);
		});

		LoadAssembly(s_ScriptData->CoreAssemblyPath);
		LoadAssemblyClasses();
	}

	void ScriptEngine::OnRuntimeStart(const Ref<Scene>& scene)
	{
		s_ScriptData->SceneContext = scene;
	}

	void ScriptEngine::OnRuntimeStop()
	{
		s_ScriptData->SceneContext.reset();
		s_ScriptData->EntityInstances.clear();

		if (s_ScriptData->ManagedClearInstances)
			s_ScriptData->ManagedClearInstances();
	}

	void ScriptEngine::OnCreateEntity(const Entity entity)
	{
		auto& scriptComponent = entity.GetComponent<ScriptComponent>();

		if (!ClassExists(scriptComponent.ClassName))
		{
			KBR_CORE_ERROR("Script class '{0}' does not exist!", scriptComponent.ClassName);
			return;
		}

		Ref<ScriptInstance> instance = nullptr;

		/// Apply initial field values when instantiating the script, if there are any
		if (const auto& fieldInitializers = GetScriptFieldInitializerMap(entity); !fieldInitializers.empty())
		{
			instance = CreateRef<ScriptInstance>(s_ScriptData->EntityClasses[scriptComponent.ClassName], entity, fieldInitializers);
		}
		else
		{
			instance = CreateRef<ScriptInstance>(s_ScriptData->EntityClasses[scriptComponent.ClassName], entity, std::unordered_map<std::string, ScriptFieldInitializer>());
		}

		const UUID entityID = entity.GetUUID();
		s_ScriptData->EntityInstances[entityID] = instance;

		instance->InvokeOnCreate();
	}

	void ScriptEngine::OnUpdateEntity(const Entity entity, const float deltaTime)
	{
		KBR_CORE_ASSERT(entity.HasComponent<ScriptComponent>(), "Entity does not have a ScriptComponent!");
		KBR_CORE_ASSERT(s_ScriptData->EntityInstances.contains(entity.GetUUID()), "No script instance found for entity!");

		s_ScriptData->EntityInstances[entity.GetUUID()]->InvokeOnUpdate(deltaTime);
	}

	void ScriptEngine::OnCollision(const Entity entity, const CollisionEvent& event) 
	{
		if (!entity.HasComponent<ScriptComponent>())
			return;

		KBR_CORE_ASSERT(s_ScriptData->EntityInstances.contains(entity.GetUUID()), "No script instance found for entity!");

		switch (event.EventType)
		{
			case CollisionEventType::Enter:
				s_ScriptData->EntityInstances[entity.GetUUID()]->InvokeOnCollisionEnter(event);
				break;
			case CollisionEventType::Persist:
				s_ScriptData->EntityInstances[entity.GetUUID()]->InvokeOnCollisionPersist(event);
				break;
			case CollisionEventType::Exit:
				s_ScriptData->EntityInstances[entity.GetUUID()]->InvokeOnCollisionExit(event);
				break;
		}
	}

	bool ScriptEngine::ClassExists(const std::string& className)
	{
		return s_ScriptData->EntityClasses.contains(className);
	}

	void ScriptEngine::CreateScriptFieldInitializers(const Entity entity, const std::string& className)
	{
		KBR_CORE_ASSERT(ClassExists(className), "Script class doesn't exist!");

		const UUID entityID = entity.GetUUID();
		const std::string_view currentClassName = entity.GetComponent<ScriptComponent>().ClassName;
		if (s_ScriptData->EntityFieldInitializers.contains(entityID) && currentClassName == className)
		{
			return;
		}

		const Ref<ScriptClass>& scriptClass = s_ScriptData->EntityClasses.at(className);
		const auto& serializedFields = scriptClass->GetSerializedFields();

		ScriptEngineData::FieldInitializerMap fieldInitializers;
		for (const auto& [name, field] : serializedFields)
		{
			ScriptFieldInitializer fieldInitializer;
			fieldInitializer.Field = field;

			fieldInitializers[name] = fieldInitializer;
		}

		s_ScriptData->EntityFieldInitializers[entityID] = fieldInitializers;
	}

	void ScriptEngine::CopyScriptFieldInitializers(const Entity srcEntity, const Entity dstEntity) 
	{
		const std::string& dstClassName = dstEntity.GetComponent<ScriptComponent>().ClassName;

		KBR_CORE_ASSERT(srcEntity.HasComponent<ScriptComponent>(), "Source entity does not have a ScriptComponent!");
		KBR_CORE_ASSERT(dstEntity.HasComponent<ScriptComponent>(), "Destination entity does not have a ScriptComponent!");
		KBR_CORE_ASSERT(srcEntity.GetComponent<ScriptComponent>().ClassName == dstClassName, "Script initializers can only be copied between entities of the same script class!");

		CreateScriptFieldInitializers(dstEntity, dstClassName);

		const auto& srcInitializers = GetScriptFieldInitializerMap(srcEntity);
		auto& dstInitializers = GetScriptFieldInitializerMap(dstEntity);
		dstInitializers = srcInitializers;
	}

	const std::unordered_map<std::string, Ref<ScriptClass>>& ScriptEngine::GetEntityClasses()
	{
		return s_ScriptData->EntityClasses;
	}

	std::unordered_map<std::string, ScriptFieldInitializer>& ScriptEngine::GetScriptFieldInitializerMap(const Entity entity)
	{
		const UUID entityID = entity.GetUUID();
		if (!s_ScriptData->EntityFieldInitializers.contains(entityID))
		{
			const std::string_view entityName = entity.GetName();
			KBR_CORE_TRACE("No field initializers found for entity {}"sv, entityName);

			return s_ScriptData->EntityFieldInitializers[entityID];
		}

		return s_ScriptData->EntityFieldInitializers.at(entityID);
	}

	Ref<ScriptInstance> ScriptEngine::GetEntityInstance(const UUID entityID)
	{
		if (!s_ScriptData->EntityInstances.contains(entityID))
		{
			return nullptr;
		}

		return s_ScriptData->EntityInstances.at(entityID);
	}

	const std::weak_ptr<Scene>& ScriptEngine::GetSceneContext()
	{
		return s_ScriptData->SceneContext;
	}

	bool ScriptEngine::CreateManagedInstance(const uint64_t entityID, const std::string& className)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedCreateInstance, "ManagedCreateInstance function pointer is null!");

		return s_ScriptData->ManagedCreateInstance(entityID, className.c_str()) != 0;
	}

	void ScriptEngine::DestroyManagedInstance(const uint64_t entityID)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedDestroyInstance, "ManagedDestroyInstance function pointer is null!");
		
		s_ScriptData->ManagedDestroyInstance(entityID);
	}

	bool ScriptEngine::InvokeManagedOnCreate(const uint64_t entityID)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedInvokeOnCreate, "ManagedInvokeOnCreate function pointer is null!");

		return s_ScriptData->ManagedInvokeOnCreate(entityID) != 0;
	}

	bool ScriptEngine::InvokeManagedOnUpdate(const uint64_t entityID, const float deltaTime)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedInvokeOnUpdate, "ManagedInvokeOnUpdate function pointer is null!");
		
		return s_ScriptData->ManagedInvokeOnUpdate(entityID, deltaTime) != 0;
	}

	bool ScriptEngine::InvokeManagedOnCollisionEnter(const uint64_t entityID, const CollisionEvent& event)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedInvokeOnCollisionEnter, "ManagedInvokeOnCollisionEnter function pointer is null!");

		return s_ScriptData->ManagedInvokeOnCollisionEnter(entityID, event) != 0;
	}

	bool ScriptEngine::InvokeManagedOnCollisionPersist(const uint64_t entityID, const CollisionEvent& event)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedInvokeOnCollisionPersist, "ManagedInvokeOnCollisionPersist function pointer is null!");

		return s_ScriptData->ManagedInvokeOnCollisionPersist(entityID, event) != 0;
	}

	bool ScriptEngine::InvokeManagedOnCollisionExit(const uint64_t entityID, const CollisionEvent& event)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedInvokeOnCollisionExit, "ManagedInvokeOnCollisionExit function pointer is null!");

		return s_ScriptData->ManagedInvokeOnCollisionExit(entityID, event) != 0;
	}

	bool ScriptEngine::GetManagedFieldValue(const uint64_t entityID, const std::string& fieldName, void* outValue, const int bufferSize)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedGetFieldValue, "ManagedGetFieldValue function pointer is null!");

		return s_ScriptData->ManagedGetFieldValue(entityID, fieldName.c_str(), outValue, bufferSize) != 0;
	}

	bool ScriptEngine::SetManagedFieldValue(const uint64_t entityID, const std::string& fieldName, void* value, const int valueSize)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedSetFieldValue, "ManagedSetFieldValue function pointer is null!");

		return s_ScriptData->ManagedSetFieldValue(entityID, fieldName.c_str(), value, valueSize) != 0;
	}

	void ScriptEngine::SetManagedNativeCallbacks(void* callbackTable)
	{
		KBR_CORE_ASSERT(s_ScriptData->ManagedSetNativeCallbacks, "ManagedSetNativeCallbacks function pointer is null!");

		s_ScriptData->ManagedSetNativeCallbacks(callbackTable);
	}

	void ScriptEngine::InitDotNet()
	{
		if (!LoadHostFxr())
		{
			KBR_CORE_ASSERT(false, "Failed to load hostfxr");
			return;
		}

		KBR_CORE_INFO("Successfully loaded .NET hostfxr");
	}

	void ScriptEngine::ShutdownDotNet()
	{
		if (s_ScriptData->HostContext)
		{
			s_ScriptData->CloseFn(s_ScriptData->HostContext);
			s_ScriptData->HostContext = nullptr;
		}
	}

	void ScriptEngine::LoadAssembly(const std::filesystem::path& assemblyPath)
	{
		s_ScriptData->CoreAssemblyPath = assemblyPath;

		/// Derive the runtime config path from the assembly path
		std::filesystem::path runtimeConfigPath = assemblyPath;
		runtimeConfigPath.replace_extension("");
		runtimeConfigPath = runtimeConfigPath.string() + ".runtimeconfig.json";
		s_ScriptData->RuntimeConfigPath = runtimeConfigPath;

		if (!std::filesystem::exists(runtimeConfigPath))
		{
			KBR_CORE_ERROR("Runtime config not found: {}", runtimeConfigPath);
			KBR_CORE_ASSERT(false, "Runtime config not found!");
			return;
		}

		/// Close any existing host context before reinitializing
		if (s_ScriptData->HostContext)
		{
			s_ScriptData->CloseFn(s_ScriptData->HostContext);
			s_ScriptData->HostContext = nullptr;
		}

		/// Initialize the .NET runtime with the runtime config
		const dotnet_string configPath = ToDotNetString(runtimeConfigPath.string());
		int rc = s_ScriptData->InitForConfigFn(configPath.c_str(), nullptr, &s_ScriptData->HostContext);
		if (rc != 0 && rc != 1) // 0 = success, 1 = already initialized (secondary context)
		{
			KBR_CORE_ERROR("Failed to initialize .NET runtime. Error: 0x{:x}", rc);
			KBR_CORE_ASSERT(false, "Failed to initialize .NET runtime!");
			return;
		}

		/// Get the load_assembly_and_get_function_pointer delegate
		rc = s_ScriptData->GetDelegateFn(
			s_ScriptData->HostContext,
			hdt_load_assembly_and_get_function_pointer,
			reinterpret_cast<void**>(&s_ScriptData->LoadAssemblyFn));

		if (rc != 0 || !s_ScriptData->LoadAssemblyFn)
		{
			KBR_CORE_ERROR("Failed to get load_assembly_and_get_function_pointer delegate. Error: 0x{:x}", rc);
			KBR_CORE_ASSERT(false, "Failed to get .NET runtime delegate!");
			return;
		}

		/// Load managed function pointers from the bridge
		LoadManagedFunctions();

		KBR_CORE_INFO("Successfully loaded .NET assembly: {}", assemblyPath);
	}

	void ScriptEngine::LoadAssemblyClasses()
	{
		s_ScriptData->EntityClasses.clear();

		if (!s_ScriptData->LoadAssemblyClasses)
		{
			KBR_CORE_ERROR("LoadAssemblyClasses managed function not loaded!");
			return;
		}

		const std::string assemblyPath = s_ScriptData->CoreAssemblyPath.string();
		const int result = s_ScriptData->LoadAssemblyClasses(assemblyPath.c_str());
		if (!result)
		{
			KBR_CORE_ERROR("Failed to load assembly classes from {}", assemblyPath);
			return;
		}

		/// Enumerate discovered classes and build ScriptClass objects
		constexpr int maxNames = 256;
		constexpr int bufferSize = 512;

		char nameBuffers[maxNames][bufferSize] = {};
		void* namePtrs[maxNames];
		for (int i = 0; i < maxNames; i++)
			namePtrs[i] = nameBuffers[i];

		const int classCount = s_ScriptData->ManagedGetEntityClassNames(namePtrs, maxNames, bufferSize);

		for (int i = 0; i < classCount; i++)
		{
			std::string fullName = nameBuffers[i];
			KBR_CORE_TRACE("Loaded C# class: {}", fullName);

			/// Parse namespace and class name from full name (e.g. "Kerberos.Source.Kerberos.Player" -> ns="Kerberos.Source.Kerberos", name="Player")
			std::string nameSpace;
			std::string className;
			const size_t lastDot = fullName.rfind('.');
			if (lastDot != std::string::npos)
			{
				nameSpace = fullName.substr(0, lastDot);
				className = fullName.substr(lastDot + 1);
			}
			else
			{
				className = fullName;
			}

			const Ref<ScriptClass> scriptClass = CreateRef<ScriptClass>(nameSpace, className, fullName);
			s_ScriptData->EntityClasses[fullName] = scriptClass;

			/// Query fields from the managed side
			constexpr int maxFields = 64;
			char fieldNameBuffers[maxFields][bufferSize] = {};
			char fieldTypeBuffers[maxFields][bufferSize] = {};
			void* fieldNamePtrs[maxFields];
			void* fieldTypePtrs[maxFields];
			for (int j = 0; j < maxFields; j++)
			{
				fieldNamePtrs[j] = fieldNameBuffers[j];
				fieldTypePtrs[j] = fieldTypeBuffers[j];
			}

			const int fieldCount = s_ScriptData->ManagedGetFields(fullName.c_str(), fieldNamePtrs, fieldTypePtrs, maxFields, bufferSize);
			for (int j = 0; j < fieldCount; j++)
			{
				const std::string fieldName = fieldNameBuffers[j];
				const std::string fieldTypeName = fieldTypeBuffers[j];

				KBR_CORE_TRACE("\tPublic field: {0}, type: {1}", fieldName, fieldTypeName);

				const ScriptFieldType fieldType = ScriptUtils::DotNetTypeToScriptFieldType(fieldTypeName);
				const ScriptField fieldInfo = { .Name = fieldName, .Type = fieldType };

				scriptClass->m_SerializedFields[fieldName] = fieldInfo;
			}
		}

		/// Register the Entity base class reference
		s_ScriptData->EntityClass = ScriptClass("Kerberos.Source.Kerberos.Scene", "Entity", "Kerberos.Source.Kerberos.Scene.Entity");
	}

	template<typename T>
	T ScriptEngine::LoadManagedFunction(const char* typeName, const char* methodName)
	{
		T fn = nullptr;
		const dotnet_string assemblyPath = ToDotNetString(s_ScriptData->CoreAssemblyPath.string());
		const dotnet_string type = ToDotNetString(typeName);
		const dotnet_string method = ToDotNetString(methodName);

		int rc = s_ScriptData->LoadAssemblyFn(
			assemblyPath.c_str(),
			type.c_str(),
			method.c_str(),
			UNMANAGEDCALLERSONLY_METHOD,
			nullptr,
			reinterpret_cast<void**>(&fn));

		if (rc != 0)
		{
			KBR_CORE_ASSERT(false, "Failed to load managed function {}.{}. Error: 0x{:x}", typeName, methodName, rc);
			KBR_CORE_ERROR("Failed to load managed function {}.{}. Error: 0x{:x}", typeName, methodName, rc);
		}

		return fn;
	}

	void ScriptEngine::LoadManagedFunctions()
	{
		const char* glueType = "Kerberos.Source.ScriptGlue, KerberosScriptCoreLib";
		const char* callbacksType = "Kerberos.Source.InternalCalls, KerberosScriptCoreLib";

		s_ScriptData->LoadAssemblyClasses = LoadManagedFunction<ManagedLoadAssemblyClassesFn>(glueType, "LoadAssemblyClasses");
		s_ScriptData->ManagedClassExists = LoadManagedFunction<ManagedClassExistsFn>(glueType, "ClassExists");
		s_ScriptData->ManagedCreateInstance = LoadManagedFunction<ManagedCreateInstanceFn>(glueType, "CreateInstance");
		s_ScriptData->ManagedDestroyInstance = LoadManagedFunction<ManagedDestroyInstanceFn>(glueType, "DestroyInstance");
		s_ScriptData->ManagedClearInstances = LoadManagedFunction<ManagedClearInstancesFn>(glueType, "ClearInstances");
		s_ScriptData->ManagedInvokeOnCreate = LoadManagedFunction<ManagedInvokeOnCreateFn>(glueType, "InvokeOnCreate");
		s_ScriptData->ManagedInvokeOnUpdate = LoadManagedFunction<ManagedInvokeOnUpdateFn>(glueType, "InvokeOnUpdate");
		s_ScriptData->ManagedInvokeOnCollisionEnter = LoadManagedFunction<ManagedInvokeOnCollisionEnterFn>(glueType, "InvokeOnCollisionEnter");
		s_ScriptData->ManagedInvokeOnCollisionPersist = LoadManagedFunction<ManagedInvokeOnCollisionPersistFn>(glueType, "InvokeOnCollisionPersist");
		s_ScriptData->ManagedInvokeOnCollisionExit = LoadManagedFunction<ManagedInvokeOnCollisionExitFn>(glueType, "InvokeOnCollisionExit");
		s_ScriptData->ManagedGetFieldCount = LoadManagedFunction<ManagedGetFieldCountFn>(glueType, "GetClassFieldCount");
		s_ScriptData->ManagedGetFields = LoadManagedFunction<ManagedGetFieldsFn>(glueType, "GetClassFields");
		s_ScriptData->ManagedGetFieldValue = LoadManagedFunction<ManagedGetFieldValueFn>(glueType, "GetFieldValue");
		s_ScriptData->ManagedSetFieldValue = LoadManagedFunction<ManagedSetFieldValueFn>(glueType, "SetFieldValue");
		s_ScriptData->ManagedGetEntityClassCount = LoadManagedFunction<ManagedGetEntityClassCountFn>(glueType, "GetEntityClassCount");
		s_ScriptData->ManagedGetEntityClassNames = LoadManagedFunction<ManagedGetEntityClassNamesFn>(glueType, "GetEntityClassNames");
		s_ScriptData->ManagedSetNativeCallbacks = LoadManagedFunction<ManagedSetNativeCallbacksFn>(callbacksType, "SetNativeCallbacks");
	}

	static std::string_view FileWatchEventToString(const filewatch::Event event)
	{
		switch (event)
		{
			case filewatch::Event::added: return "added"sv;
			case filewatch::Event::removed: return "removed"sv;
			case filewatch::Event::modified: return "modified"sv;
			case filewatch::Event::renamed_old: return "renamed old"sv;
			case filewatch::Event::renamed_new: return "renamed new"sv;
		}

		KBR_CORE_ASSERT(false, "Unknown filewatch::Event");
		return "Unknown"sv;
	}

	void ScriptEngine::OnAssemblyFileChanged(const std::string& path, const filewatch::Event changeType)
	{
		const std::string extension = std::filesystem::path(path).extension().string();
		if (extension == ".dll") {
			KBR_CORE_INFO("Assembly file {0}: {1}", FileWatchEventToString(changeType), path);
			Application::Get().SubmitToMainThreadQueue([]() {
				ReloadAssembly();
			});
		}
	}
}

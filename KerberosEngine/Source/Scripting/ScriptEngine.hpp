#pragma once

#include "Core/UUID.hpp"
#include "Scene/Entity.hpp"
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <functional>


namespace filewatch { enum class Event; }
namespace filewatch { template<typename T> class FileWatch; }

namespace Kerberos { class ScriptClass; }
namespace Kerberos { class ScriptInstance; }
namespace Kerberos { class ScriptInterface; }
namespace Kerberos { struct ScriptFieldInitializer; }

namespace Kerberos
{
	/// Function pointer types for calls into the managed ScriptGlue bridge
	using ManagedLoadAssemblyClassesFn = int(*)(const char* assemblyPath);
	using ManagedClassExistsFn = int(*)(const char* className);
	using ManagedCreateInstanceFn = int(*)(uint64_t entityID, const char* className);
	using ManagedDestroyInstanceFn = void(*)(uint64_t entityID);
	using ManagedClearInstancesFn = void(*)();
	using ManagedInvokeOnCreateFn = int(*)(uint64_t entityID);
	using ManagedInvokeOnUpdateFn = int(*)(uint64_t entityID, float deltaTime);
	using ManagedInvokeOnCollisionEnterFn = int(*)(uint64_t entityID, const CollisionEvent& event);
	using ManagedInvokeOnCollisionPersistFn = int(*)(uint64_t entityID, const CollisionEvent& event);
	using ManagedInvokeOnCollisionExitFn = int(*)(uint64_t entityID, const CollisionEvent& event);
	using ManagedGetFieldCountFn = int(*)(const char* className);
	using ManagedGetFieldsFn = int(*)(const char* className, void** fieldNames, void** fieldTypeNames, int maxFields, int bufferSize);
	using ManagedGetFieldValueFn = int(*)(uint64_t entityID, const char* fieldName, void* outValue, int bufferSize);
	using ManagedSetFieldValueFn = int(*)(uint64_t entityID, const char* fieldName, void* value, int valueSize);
	using ManagedGetEntityClassCountFn = int(*)();
	using ManagedGetEntityClassNamesFn = int(*)(void** namesBuffer, int maxNames, int bufferSize);
	using ManagedSetNativeCallbacksFn = void(*)(void* nativeCallbacks);

	class ScriptEngine
	{
	public:
		static void Init();
		static void Shutdown();
		static void ReloadAssembly();

		static void OnRuntimeStart(const Ref<Scene>& scene);
		static void OnRuntimeStop();

		static void OnCreateEntity(Entity entity);
		static void OnUpdateEntity(Entity entity, float deltaTime);

		static void OnCollision(Entity entity, const CollisionEvent& event);

		static bool ClassExists(const std::string& className);
		static void CreateScriptFieldInitializers(Entity entity, const std::string& className);
		static void CopyScriptFieldInitializers(Entity srcEntity, Entity dstEntity);

		static const std::unordered_map<std::string, Ref<ScriptClass>>& GetEntityClasses();
		static std::unordered_map<std::string, ScriptFieldInitializer>& GetScriptFieldInitializerMap(Entity entity);
		static Ref<ScriptInstance> GetEntityInstance(UUID entityID);
		static const WeakRef<Scene>& GetSceneContext();

		/// Managed bridge accessors (used by ScriptInstance)
		static bool CreateManagedInstance(uint64_t entityID, const std::string& className);
		static void DestroyManagedInstance(uint64_t entityID);
		static bool InvokeManagedOnCreate(uint64_t entityID);
		static bool InvokeManagedOnUpdate(uint64_t entityID, float deltaTime);
		static bool InvokeManagedOnCollisionEnter(uint64_t entityID, const CollisionEvent& event);
		static bool InvokeManagedOnCollisionPersist(uint64_t entityID, const CollisionEvent& event);
		static bool InvokeManagedOnCollisionExit(uint64_t entityID, const CollisionEvent& event);
		static bool GetManagedFieldValue(uint64_t entityID, const std::string& fieldName, void* outValue, int bufferSize);
		static bool SetManagedFieldValue(uint64_t entityID, const std::string& fieldName, void* value, int valueSize);

		/// Passes the native callback table to the managed side
		static void SetManagedNativeCallbacks(void* callbackTable);

	private:
		static void InitDotNet();
		static void ShutdownDotNet();

		static void LoadAssembly(const std::filesystem::path& assemblyPath);
		static void LoadAssemblyClasses();

		static void OnAssemblyFileChanged(const std::string& path, const filewatch::Event changeType);

		/// Loads a single function pointer from the managed assembly
		template<typename T>
		static T LoadManagedFunction(const char* typeName, const char* methodName);

		/// Loads all managed bridge function pointers
		static void LoadManagedFunctions();

		friend class ScriptClass;
		friend class ScriptInterface;
	};
}

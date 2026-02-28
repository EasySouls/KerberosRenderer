#include <KerberosEngine.hpp>
#include <EntryPoint.hpp>

#include "EditorLayer.hpp"

namespace Kerberos
{
	class KerberosEditorApp : public Application
	{
	public:
		explicit KerberosEditorApp(const ApplicationSpecification& spec)
			: Application(spec)
		{
			PushLayer<EditorLayer>();
		}

		~KerberosEditorApp() override = default;
		
		KerberosEditorApp(const KerberosEditorApp& other) = delete;
		KerberosEditorApp(KerberosEditorApp&& other) noexcept = delete;
		KerberosEditorApp& operator=(const KerberosEditorApp& other) = delete;
		KerberosEditorApp& operator=(KerberosEditorApp&& other) noexcept = delete;
	};

	Application* CreateApplication(const ApplicationCommandLineArgs args)
	{
		ApplicationSpecification spec;
		spec.Name = "Kerberos Editor";
		spec.CommandLineArgs = args;

		// Workaround for the fact that the editor is launched inside CMake's build	directory.
		// We have to go back to the project root directory so the editor can find the assets and other files it needs.
		const std::filesystem::path projectRoot = std::filesystem::current_path()
			.parent_path()
			.parent_path()
			.parent_path()
			.append("KerberosEditor");

		spec.WorkingDirectory = projectRoot;

		/*/// This is only needed so when the editor initializes, the static project shouldn't be null
		Project::New();*/

		return new KerberosEditorApp(spec);
	}
}

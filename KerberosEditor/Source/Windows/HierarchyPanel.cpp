#include "HierarchyPanel.hpp"

#include "AssetConstants.hpp"
#include "Logging/Log.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Components/AudioComponents.hpp"
#include "Assets/AssetManager.hpp"
#include "Input/InputSystem.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Assets/AssetManager.hpp"
#include "Events/KeyPressedEvent.hpp"
//#include "Scripting/ScriptEngine.hpp"
//#include "Scripting/ScriptInstance.hpp"
//#include "Scripting/ScriptClass.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <filesystem>


namespace Kerberos
{
	HierarchyPanel::HierarchyPanel(const Ref<Scene>& context)
		: m_Context(context)
	{
		SetContext(context);
	}

	void HierarchyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;

		/// This is neccessary to avoid having a dangling pointer to the previous context
		/// when loading a new scene
		m_SelectedEntity = {};

		m_IceTexture = TextureImporter::ImportTexture("assets/textures/y2k_ice_texture.png");
		m_SpriteSheetTexture = TextureImporter::ImportTexture("assets/game/textures/RPGpack_sheet_2X.png");
		/*m_CubeMesh = Mesh::CreateCube(1.0f);
		m_SphereMesh = Mesh::CreateSphere(1.0f, 16, 16);*/
		m_CubeMesh = nullptr;
		m_SphereMesh = nullptr;
		m_WhiteMaterial = CreateRef<Material>();

		m_WhiteTexture = AssetManager::GetDefaultTexture2D();

		/*FramebufferSpecification frameBufferSpec;
		frameBufferSpec.Width = 256;
		frameBufferSpec.Height = 256;
		frameBufferSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
		m_CubemapFramebuffer = Framebuffer::Create(frameBufferSpec);
		m_CubemapFramebuffer->SetDebugName("Depth Map Visualization");*/
	}

	void HierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Hierarchy");

		/// Only draw the root nodes
		for (const entt::entity& entityId : m_Context->GetRootEntities())
		{
			Entity rootEntity{ entityId, m_Context.get() };
			DrawEntityNode(rootEntity);
		}

		//for (const auto entityId : m_Context->m_Registry.view<entt::entity>())
		//{
		//	Entity e{ entityId, m_Context.get() };

		//	if (!m_Context->GetParent(e))
		//		DrawEntityNode(e);
		//}

		/// If an empty space is clicked, deselect the entity
		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		{
			m_SelectedEntity = {};
		}

		/// If the right mouse button is clicked, show the context menu
		if (ImGui::BeginPopupContextWindow(nullptr, 1 | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Empty Entity"))
			{
				m_Context->CreateEntity("Empty Entity");
			}

			ImGui::EndPopup();
		}
		ImGui::End();

		ImGui::Begin("Properties");
		if (m_SelectedEntity)
		{
			DrawComponents(m_SelectedEntity);

			AddComponentPopup(m_SelectedEntity);
		}
		ImGui::End();

		for (const auto& e : m_DeletionQueue)
		{
			m_NotificationManager.AddNotification("Entity deleted: " + e.GetComponent<TagComponent>().Tag, Notification::Type::Info);
			m_Context->DestroyEntity(e);
		}
		m_DeletionQueue.clear();

		m_NotificationManager.RenderNotifications();
	}

	void HierarchyPanel::SetSelectedEntity(const Entity entity)
	{
		m_SelectedEntity = entity;
	}

	void HierarchyPanel::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(KBR_BIND_FN(HierarchyPanel::OnKeyPressed));
	}

	void HierarchyPanel::DrawEntityNode(const Entity& entity)
	{
		const auto& tag = entity.GetComponent<TagComponent>().Tag;

		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
			| (entity == m_SelectedEntity ? ImGuiTreeNodeFlags_Selected : 0);

		/// The entity's identifier serves as the unique ID for the ImGui tree node
		const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint64_t>(entity.GetUUID())), flags, "%s", tag.c_str());

		if (ImGui::IsItemClicked())
		{
			m_SelectedEntity = entity;
		}

		bool entityDeleted = false;

		/// Context menu on entity right-click
		{

			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Delete Entity", "Delete"))
				{
					/// Defer the deletion of the entity until the popup is closed to avoid invalidating the iterator
					entityDeleted = true;
				}
				if (ImGui::MenuItem("Duplicate Entity", "Ctrl + D"))
				{
					m_Context->DuplicateEntity(entity, true);
				}
				if (ImGui::MenuItem("Create child"))
				{
					m_Context->CreateChild(entity);
				}
				ImGui::EndPopup();
			}
		}

		if (opened)
		{
			for (const Entity& child : m_Context->GetChildren(entity))
			{
				DrawEntityNode(child);
			}

			ImGui::TreePop();
		}

		if (entityDeleted)
		{
			m_DeletionQueue.push_back(entity);
			if (m_SelectedEntity == entity)
			{
				m_SelectedEntity = {};
			}
		}
	}

	void HierarchyPanel::AddComponentPopup(Entity entity)
	{
		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("Add Component");
		}

		if (ImGui::BeginPopup("Add Component"))
		{
			if (ImGui::MenuItem("Camera"))
			{
				entity.AddComponent<CameraComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Script"))
			{
				entity.AddComponent<ScriptComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Sprite Renderer"))
			{
				entity.AddComponent<SpriteRendererComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Directional Light"))
			{
				entity.AddComponent<DirectionalLightComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Point Light"))
			{
				entity.AddComponent<PointLightComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Static Mesh"))
			{
				entity.AddComponent<StaticMeshComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginMenu("Physics"))
			{

				if (ImGui::MenuItem("Rigidbody3D"))
				{
					entity.AddComponent<RigidBody3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Box Collider 3D"))
				{
					entity.AddComponent<BoxCollider3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Sphere Collider 3D"))
				{
					entity.AddComponent<SphereCollider3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Capsule Collider 3D"))
				{
					entity.AddComponent<CapsuleCollider3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Mesh Collider 3D"))
				{
					entity.AddComponent<MeshCollider3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Environment"))
			{
				entity.AddComponent<EnvironmentComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Text"))
			{
				entity.AddComponent<TextComponent>();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::BeginMenu("Audio"))
			{
				if (ImGui::MenuItem("Audio Source 2D"))
				{
					entity.AddComponent<AudioSource2DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Audio Source 3D"))
				{
					entity.AddComponent<AudioSource3DComponent>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Audio Listener"))
				{
					entity.AddComponent<AudioListenerComponent>();
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
	}

	bool HierarchyPanel::OnKeyPressed(const KeyPressedEvent& event)
	{
		/// Shortcuts
		if (event.GetRepeatCount() > 0)
			return false;

		const bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		const bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		switch (event.GetKeyCode())
		{
			case Key::Delete:
				if (m_SelectedEntity)
				{
					m_DeletionQueue.push_back(m_SelectedEntity);
					m_SelectedEntity = {};
					return true;
				}
				break;
			case Key::D:
				if (m_SelectedEntity && ctrl)
				{
					m_Context->DuplicateEntity(m_SelectedEntity, true);
					return true;
				}
				break;
			default:
				break;
		}

		return false;
	}

	static void DrawVec3Control(const std::string& label, glm::vec3& values, const float resetValue = 0.0f, const float columnWidth = 80.0f, const std::function<void()>& onValueChanged = nullptr)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const auto boldFont = io.Fonts->Fonts[1];

		ImGui::PushID(label.c_str());

		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth);
		ImGui::Text("%s", label.c_str());
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

		const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
		const ImVec2 buttonSize = ImVec2(lineHeight + 2.0f, lineHeight);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.25f, 1.0f));

		ImGui::PushFont(boldFont);
		if (ImGui::Button("X", buttonSize))
		{
			values.x = resetValue;
			if (onValueChanged)
				onValueChanged();
		}
		ImGui::PopFont();

		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		/// ##X is used to hide the label of the input field
		if (ImGui::DragFloat("##X", &values.x, 0.1f))
		{
			if (onValueChanged)
				onValueChanged();
		}

		ImGui::PopItemWidth();
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.9f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.8f, 0.2f, 1.0f));

		ImGui::PushFont(boldFont);
		if (ImGui::Button("Y", buttonSize))
		{
			values.y = resetValue;
			if (onValueChanged)
				onValueChanged();
		}
		ImGui::PopFont();

		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		/// ##Y is used to hide the label of the input field
		if (ImGui::DragFloat("##Y", &values.y, 0.1f))
		{
			if (onValueChanged)
				onValueChanged();
		}

		ImGui::PopItemWidth();
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.9f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.25f, 0.8f, 1.0f));

		ImGui::PushFont(boldFont);
		if (ImGui::Button("Z", buttonSize))
		{
			values.z = resetValue;
			if (onValueChanged)
				onValueChanged();
		}
		ImGui::PopFont();

		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		/// ##Z is used to hide the label of the input field
		if (ImGui::DragFloat("##Z", &values.z, 0.1f))
		{
			if (onValueChanged)
				onValueChanged();
		}
		ImGui::PopItemWidth();

		ImGui::PopStyleVar();

		/// Reset the column value to be the default
		ImGui::Columns(1);

		ImGui::PopID();
	}

	void HierarchyPanel::DrawComponents(const Entity entity)
	{
		if (entity.HasComponent<TagComponent>())
		{
			if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TagComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "Tag"))
			{
				auto& tag = entity.GetComponent<TagComponent>().Tag;

				char buffer[256];
				strcpy_s(buffer, sizeof(buffer), tag.c_str());
				if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
				{
					tag = buffer;
				}

				ImGui::TreePop();
			}

		}
		if (entity.HasComponent<TransformComponent>())
		{
			if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TransformComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "Transform"))
			{
				auto& transform = entity.GetComponent<TransformComponent>();
				const auto onValueChanged = [&entity, this]()
				{
					m_Context->CalculateEntityTransform(entity);
				};

				DrawVec3Control("Position", transform.Translation, 0.0f, 80.0f, onValueChanged);
				DrawVec3Control("Rotation", transform.Rotation, 0.0f, 80.0f, onValueChanged);
				DrawVec3Control("Scale", transform.Scale, 1.0f, 80.f, onValueChanged);

				ImGui::TreePop();
			}
		}

		constexpr ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;

		if (entity.HasComponent<CameraComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code()), treeNodeFlags, "Camera");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Camera Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& cameraComponent = entity.GetComponent<CameraComponent>();
				auto& camera = cameraComponent.Camera;

				const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
				const char* currentProjectionTypeString = projectionTypeStrings[static_cast<int>(camera.GetProjectionType())];

				if (ImGui::BeginCombo("Projection Type", currentProjectionTypeString))
				{
					for (int i = 0; i < 2; i++)
					{
						const bool isSelected = (currentProjectionTypeString == projectionTypeStrings[i]);
						if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
						{
							camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(i));
						}
						if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
				{
					float size = camera.GetOrthographicSize();
					if (ImGui::DragFloat("Size", &size))
					{
						camera.SetOrthographicSize(size);
					}
					float nearClip = camera.GetOrthographicNearClip();
					if (ImGui::DragFloat("Near Clip", &nearClip))
					{
						camera.SetOrthographicNearClip(nearClip);
					}
					float farClip = camera.GetOrthographicFarClip();
					if (ImGui::DragFloat("Far Clip", &farClip))
					{
						camera.SetOrthographicFarClip(farClip);
					}
				}
				else
				{
					float fov = glm::degrees(camera.GetPerspectiveFov());
					if (ImGui::DragFloat("FOV", &fov))
					{
						camera.SetPerspectiveFov(glm::radians(fov));
					}
					float nearClip = camera.GetPerspectiveNearClip();
					if (ImGui::DragFloat("Near Clip", &nearClip))
					{
						camera.SetPerspectiveNearClip(nearClip);
					}
					float farClip = camera.GetPerspectiveFarClip();
					if (ImGui::DragFloat("Far Clip", &farClip))
					{
						camera.SetPerspectiveFarClip(farClip);
					}
				}

				ImGui::Checkbox("Primary", &cameraComponent.IsPrimary);
				ImGui::Checkbox("Fixed Aspect Ratio", &cameraComponent.FixedAspectRatio);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<CameraComponent>();
			}
		}

		/*if (entity.HasComponent<ScriptComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<const void*>(typeid(ScriptComponent).hash_code()), treeNodeFlags, "Script");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Script Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& component = entity.GetComponent<ScriptComponent>();

				bool scriptClassExists = false;
				const auto& entityClasses = ScriptEngine::GetEntityClasses();

				if (entityClasses.contains(component.ClassName))
				{
					scriptClassExists = true;
					ScriptEngine::CreateScriptFieldInitializers(entity, component.ClassName);
				}

				static char buffer[64];
				strcpy_s(buffer, component.ClassName.c_str());

				if (!scriptClassExists)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.3f, 1.0f));
				}

				if (ImGui::InputText("Class", buffer, sizeof(buffer)))
				{
					if (entityClasses.contains(buffer))
					{
						ScriptEngine::CreateScriptFieldInitializers(entity, buffer);
					}
					component.ClassName = buffer;
				}

				if (scriptClassExists)
				{
					/// We decide whether to show the initializer fields based on whether the script instance exists
					/// This is basically just a check if the scene is running or not,
					/// since script instances are only created when the scene is running
					if (const Ref<ScriptInstance>& scriptInstance = ScriptEngine::GetEntityInstance(entity.GetUUID()))
					{
						/// The scene is running, show the live fields for the script instance

						/// Fields
						const auto& fields = scriptInstance->GetScriptClass()->GetSerializedFields();
						for (const auto& [name, scriptField] : fields)
						{
							if (scriptField.Type == ScriptFieldType::Int)
							{
								int value = scriptInstance->GetFieldValue<int>(name);
								if (ImGui::InputInt(name.c_str(), &value))
								{
									scriptInstance->SetFieldValue<int>(name, value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Float)
							{
								float value = scriptInstance->GetFieldValue<float>(name);
								if (ImGui::DragFloat(name.c_str(), &value, 0.1f))
								{
									scriptInstance->SetFieldValue<float>(name, value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Bool)
							{
								bool value = scriptInstance->GetFieldValue<bool>(name);
								if (ImGui::Checkbox(name.c_str(), &value))
								{
									scriptInstance->SetFieldValue<bool>(name, value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::String)
							{
								std::string value = scriptInstance->GetFieldValue<std::string>(name);
								char strBuffer[256];
								strcpy_s(strBuffer, sizeof(strBuffer), value.c_str());
								if (ImGui::InputText(name.c_str(), strBuffer, sizeof(strBuffer)))
								{
									scriptInstance->SetFieldValue<std::string>(name, std::string(strBuffer));
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec2)
							{
								glm::vec2 value = scriptInstance->GetFieldValue<glm::vec2>(name);
								if (ImGui::DragFloat2(name.c_str(), glm::value_ptr(value), 0.1f))
								{
									scriptInstance->SetFieldValue<glm::vec2>(name, value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec3)
							{
								glm::vec3 value = scriptInstance->GetFieldValue<glm::vec3>(name);
								if (ImGui::DragFloat3(name.c_str(), glm::value_ptr(value), 0.1f))
								{
									scriptInstance->SetFieldValue<glm::vec3>(name, value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec4)
							{
								glm::vec4 value = scriptInstance->GetFieldValue<glm::vec4>(name);
								if (ImGui::DragFloat3(name.c_str(), glm::value_ptr(value), 0.1f))
								{
									scriptInstance->SetFieldValue<glm::vec4>(name, value);
								}
							}
							else
							{
								ImGui::Text("Unsupported field type");
							}
						}
					}
					else
					{
						/// The scene is not running, show the initializer fields for the script,
						/// which then will be applied to the script when the scene starts.

						const auto& fields = ScriptEngine::GetScriptFieldInitializerMap(entity);
						for (const auto& [fieldName, fieldInitializer] : fields)
						{
							const auto& scriptField = fieldInitializer.Field;

							if (scriptField.Type == ScriptFieldType::Int)
							{
								int value = fieldInitializer.GetValue<int>();
								if (ImGui::InputInt(fieldName.c_str(), &value))
								{
									fieldInitializer.SetValue<int>(value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Float)
							{
								float value = fieldInitializer.GetValue<float>();
								if (ImGui::DragFloat(fieldName.c_str(), &value, 0.1f))
								{
									fieldInitializer.SetValue<float>(value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Bool)
							{
								bool value = fieldInitializer.GetValue<bool>();
								if (ImGui::Checkbox(fieldName.c_str(), &value))
								{
									fieldInitializer.SetValue<bool>(value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::String)
							{
								std::string value = fieldInitializer.GetValue<std::string>();
								char strBuffer[256];
								strcpy_s(strBuffer, sizeof(strBuffer), value.c_str());
								if (ImGui::InputText(fieldName.c_str(), strBuffer, sizeof(strBuffer)))
								{
									fieldInitializer.SetValue<std::string>(std::string(strBuffer));
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec2)
							{
								glm::vec2 value = fieldInitializer.GetValue<glm::vec2>();
								if (ImGui::DragFloat2(fieldName.c_str(), glm::value_ptr(value), 0.1f))
								{
									fieldInitializer.SetValue<glm::vec2>(value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec3)
							{
								glm::vec3 value = fieldInitializer.GetValue<glm::vec3>();
								if (ImGui::DragFloat3(fieldName.c_str(), glm::value_ptr(value), 0.1f))
								{
									fieldInitializer.SetValue<glm::vec3>(value);
								}
							}
							else if (scriptField.Type == ScriptFieldType::Vec4)
							{
								glm::vec4 value = fieldInitializer.GetValue<glm::vec4>();
								if (ImGui::DragFloat4(fieldName.c_str(), glm::value_ptr(value), 0.1f))
								{
									fieldInitializer.SetValue<glm::vec4>(value);
								}
							}
							else
							{
								ImGui::Text("Unsupported field type");
							}
						}
					}
				}

				if (!scriptClassExists)
				{
					ImGui::PopStyleColor();
				}

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<ScriptComponent>();
			}
		}*/

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(SpriteRendererComponent).hash_code()), treeNodeFlags, "Sprite");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Sprite Renderer Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& spriteRenderer = entity.GetComponent<SpriteRendererComponent>();
				ImGui::ColorEdit4("Color", &spriteRenderer.Color[0]);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<SpriteRendererComponent>();
			}
		}
		if (entity.HasComponent<DirectionalLightComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(DirectionalLightComponent).hash_code()), treeNodeFlags, "Directional Light");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Directional Light Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& directionalLight = entity.GetComponent<DirectionalLightComponent>();
				ImGui::ColorEdit3("Color", &directionalLight.Light.Color[0]);
				if (ImGui::DragFloat("Intensity", &directionalLight.Light.Intensity, 0.01f, 0.0f, 10.0f))
				{
					directionalLight.NeedsUpdate = true;
				}
				DrawVec3Control("Direction", directionalLight.Light.Direction, 0.0f, 80.0f, [&directionalLight]()
				{
					directionalLight.NeedsUpdate = true;
				});
				ImGui::Checkbox("Enabled", &directionalLight.IsEnabled);
				ImGui::Checkbox("Cast Shadows", &directionalLight.CastShadows);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<DirectionalLightComponent>();
			}
		}
		if (entity.HasComponent<PointLightComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(PointLightComponent).hash_code()), treeNodeFlags, "Point Light");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Point Light Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& pointLight = entity.GetComponent<PointLightComponent>();
				ImGui::Checkbox("Enabled", &pointLight.IsEnabled);
				ImGui::ColorEdit3("Color", &pointLight.Light.Color[0]);
				DrawVec3Control("Position", pointLight.Light.Position);
				ImGui::DragFloat("Intensity", &pointLight.Light.Intensity, 0.01f, 0.0f, 10.0f);
				ImGui::Text("Attenuation factors");
				ImGui::DragFloat("Constant", &pointLight.Light.Constant, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Linear", &pointLight.Light.Linear, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Quadratic", &pointLight.Light.Quadratic, 0.01f, 0.0f, 1.0f);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<PointLightComponent>();
			}
		}
		if (entity.HasComponent<SpotLightComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(SpotLightComponent).hash_code()), treeNodeFlags, "Spotlight");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Directional Light Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& spotlight = entity.GetComponent<SpotLightComponent>();
				ImGui::ColorEdit3("Color", &spotlight.Light.Color[0]);
				ImGui::DragFloat("Intensity", &spotlight.Light.Intensity, 0.01f, 0.0f, 10.0f);
				DrawVec3Control("Position", spotlight.Light.Position);
				ImGui::Checkbox("Enabled", &spotlight.IsEnabled);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<SpotLightComponent>();
			}
		}
		if (entity.HasComponent<StaticMeshComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(StaticMeshComponent).hash_code()), treeNodeFlags, "Static Mesh");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Static Mesh Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& staticMesh = entity.GetComponent<StaticMeshComponent>();
				ImGui::Checkbox("Visible", &staticMesh.Visible);

				/// TODO: Visualize the static mesh in the editor
				ImGui::Button("Static Mesh");

				/// Handle drag and drop for meshes
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserMesh))
					{
						const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
						if (AssetManager::GetAssetType(handle) != AssetType::Mesh)
						{
							KBR_EDITOR_ERROR("Asset is not a mesh: {0}", handle);
							m_NotificationManager.AddNotification("Asset is not a mesh", Notification::Type::Error);
							return;
						}
						const Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(handle);
						staticMesh.StaticMesh = mesh;
					}
					ImGui::EndDragDropTarget();
				}

				std::string meshLabel = "None";
				if (staticMesh.StaticMesh)
				{
					if (AssetManager::IsAssetHandleValid(staticMesh.StaticMesh->GetHandle()))
					{
						const auto& [Type, Filepath] = Project::GetActive()->GetEditorAssetManager()->GetMetadata(staticMesh.StaticMesh->GetHandle());
						meshLabel = Filepath.filename().string();
					}
					else
					{
						meshLabel = "Invalid Mesh";
					}
				}
				ImGui::Text("%s", meshLabel.c_str());

				ImGui::Separator();

				ImGui::Checkbox("Cast Shadows", &staticMesh.CastShadows);

				if (staticMesh.MeshMaterial)
				{
					ImGui::Separator();
					ImGui::Text("Material");
					/*ImGui::ColorEdit3("Diffuse", &staticMesh.MeshMaterial->Diffuse[0]);
					ImGui::ColorEdit3("Ambient", &staticMesh.MeshMaterial->Ambient[0]);
					ImGui::ColorEdit3("Specular", &staticMesh.MeshMaterial->Specular[0]);
					ImGui::DragFloat("Shininess", &staticMesh.MeshMaterial->Shininess, 0.1f, 0.0f, 10.0f);*/
				}

				ImGui::Separator();
				ImGui::Text("Texture");

				std::string textureLabel = "None";
				/*uint64_t textureID;
				if (staticMesh.MeshTexture)
				{
					if (AssetManager::IsAssetHandleValid(staticMesh.MeshTexture->GetHandle()))
					{
						const auto& [Type, Filepath] = Project::GetActive()->GetEditorAssetManager()->GetMetadata(staticMesh.MeshTexture->GetHandle());
						textureLabel = Filepath.filename().string();
					}
					else
					{
						textureLabel = "Invalid texture";
					}

					textureID = staticMesh.MeshTexture->GetRendererID();
				}
				else
				{
					textureID = m_WhiteTexture->GetRendererID();
				}*/
				ImGui::Text("%s", textureLabel.c_str());

				//ImGui::Image(textureID, ImVec2{ 64, 64 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

				/// Handle drag and drop for textures
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserTexture))
					{
						const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
						if (AssetManager::GetAssetType(handle) != AssetType::Texture2D)
						{
							KBR_EDITOR_ERROR("Asset is not a texture: {0}", handle);
							m_NotificationManager.AddNotification("Asset is not a texture", Notification::Type::Error);
							return;
						}
						const Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
						staticMesh.MeshTexture = texture;
					}
					ImGui::EndDragDropTarget();
				}

				/// Clear the texture if needed
				if (ImGui::BeginPopupContextItem("Texture Options"))
				{
					ImGui::TextDisabled("%s", "Texture Options");
					ImGui::Separator();
					if (ImGui::MenuItem("Clear Texture"))
					{
						staticMesh.MeshTexture = m_WhiteTexture;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<StaticMeshComponent>();
			}
		}
		if (entity.HasComponent<RigidBody3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(RigidBody3DComponent).hash_code()), treeNodeFlags, "RigidBody3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("RigidBody3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& rb = entity.GetComponent<RigidBody3DComponent>();

				const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
				const char* currentBodyTypeString = bodyTypeStrings[static_cast<int>(rb.Type)];
				if (ImGui::BeginCombo("Body Type", currentBodyTypeString))
				{
					for (int i = 0; i < 3; i++)
					{
						const bool isSelected = (currentBodyTypeString == bodyTypeStrings[i]);
						if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
						{
							rb.Type = static_cast<RigidBody3DComponent::BodyType>(i);
						}
						if (isSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}

				ImGui::DragFloat("Friction", &rb.Friction, 0.02f, 0.0f, 1.0f);
				ImGui::DragFloat("Restitution", &rb.Restitution, 0.02f, 0.0f, 1.0f);
				ImGui::DragFloat3("Velocity", glm::value_ptr(rb.Velocity), 0.1f);
				ImGui::DragFloat3("Angular Velocity", glm::value_ptr(rb.AngularVelocity));
				ImGui::Checkbox("Use Gravity", &rb.UseGravity);
				ImGui::DragFloat("Mass", &rb.Mass, 0.1f, 0.01f, 100.0f);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<RigidBody3DComponent>();
			}
		}
		if (entity.HasComponent<BoxCollider3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(BoxCollider3DComponent).hash_code()), treeNodeFlags, "BoxCollider3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();

			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("BoxCollider3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (opened)
			{
				auto& collider = entity.GetComponent<BoxCollider3DComponent>();
				ImGui::DragFloat3("Size", glm::value_ptr(collider.Size), 0.1f);
				ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
				ImGui::Checkbox("Is Trigger", &collider.IsTrigger);

				ImGui::TreePop();
			}

			if (componentDeleted)
			{
				entity.RemoveComponent<BoxCollider3DComponent>();
			}
		}
		if (entity.HasComponent<SphereCollider3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(SphereCollider3DComponent).hash_code()), treeNodeFlags, "SphereCollider3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("SphereCollider3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& collider = entity.GetComponent<SphereCollider3DComponent>();
				ImGui::DragFloat("Radius", &collider.Radius, 0.1f);
				ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
				ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<SphereCollider3DComponent>();
			}
		}
		if (entity.HasComponent<CapsuleCollider3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CapsuleCollider3DComponent).hash_code()), treeNodeFlags, "CapsuleCollider3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("CapsuleCollider3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& collider = entity.GetComponent<CapsuleCollider3DComponent>();
				ImGui::DragFloat("Radius", &collider.Radius, 0.1f);
				ImGui::DragFloat("Height", &collider.Height, 0.1f);
				ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
				ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<CapsuleCollider3DComponent>();
			}
		}
		if (entity.HasComponent<MeshCollider3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(MeshCollider3DComponent).hash_code()), treeNodeFlags, "MeshCollider3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("MeshCollider3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& collider = entity.GetComponent<MeshCollider3DComponent>();
				ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
				std::string meshLabel = "None";
				if (collider.Mesh)
				{
					if (AssetManager::IsAssetHandleValid(collider.Mesh->GetHandle()))
					{
						const auto& [Type, Filepath] = Project::GetActive()->GetEditorAssetManager()->GetMetadata(collider.Mesh->GetHandle());
						meshLabel = Filepath.filename().string();
					}
					else
					{
						meshLabel = "Invalid Mesh";
					}
				}
				ImGui::Text("Mesh: %s", meshLabel.c_str());
				ImGui::Separator();
				//if (ImGui::Button("Select Mesh"))
				//{
				//	m_Context->SetSelectedEntity(entity);
				//	m_Context->OpenAssetBrowser(AssetType::Mesh);
				//}
				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<MeshCollider3DComponent>();
			}
		}
		if (entity.HasComponent<EnvironmentComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(EnvironmentComponent).hash_code()), treeNodeFlags, "Environment");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Environment Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& environment = entity.GetComponent<EnvironmentComponent>();
				ImGui::Checkbox("Skybox Enabled", &environment.IsSkyboxEnabled);

				/// Render the environment cubemap into an image
				ImGui::Text("Skybox Texture");
				/*const uint64_t textureID = m_CubemapFramebuffer->GetColorAttachmentRendererID();
				ImGui::Image(textureID, ImVec2{ 256, 256 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });*/

				/// Handle drag and drop for textures
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserTextureCube))
					{
						const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
						if (AssetManager::GetAssetType(handle) != AssetType::TextureCube)
						{
							KBR_EDITOR_ERROR("Asset is not a texture: {0}", handle);
							m_NotificationManager.AddNotification("Asset is not a cubemap", Notification::Type::Error);
							return;
						}
						environment.SkyboxTexture = handle;

						/// render the changed cube into the environment
						/*m_CubemapFramebuffer->Bind();
						m_CubemapFramebuffer->Unbind();*/
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<EnvironmentComponent>();
			}
		}
		if (entity.HasComponent<TextComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TextComponent).hash_code()), treeNodeFlags, "Text");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Text Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& text = entity.GetComponent<TextComponent>();

				char buffer[256];
				strcpy_s(buffer, sizeof(buffer), text.Text.c_str());
				if (ImGui::InputText("Text", buffer, sizeof(buffer)))
				{
					text.Text = buffer;
				}

				ImGui::Separator();

				ImGui::DragFloat4("Color", &text.Color[0], 0.01f, 0.0f, 1.0f);

				ImGui::Separator();

				ImGui::DragFloat("FontSize", &text.FontSize);

				ImGui::Separator();

				ImGui::Text("Font: %s", text.Font->GetName().c_str());
				//ImGui::Image(text.Font->GetAtlasTexture()->GetRendererID(), ImVec2{ 128, 128 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

				///// Handle drag and drop for fonts
				//if (ImGui::BeginDragDropTarget())
				//{
				//	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserFont))
				//	{
				//		const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
				//		if (AssetManager::GetAssetType(handle) != AssetType::Texture2D) // TODO: Change to font type when it exists
				//		{
				//			KBR_EDITOR_ERROR("Asset is not a texture: {0}", handle);
				//			m_NotificationManager.AddNotification("Asset is not a font", Notification::Type::Error);
				//			return;
				//		}
				//		//text.Font = AssetManager::GetAsset<Font>(handle);
				//	}
				//	ImGui::EndDragDropTarget();
				//}

				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<EnvironmentComponent>();
			}
		}
		if (entity.HasComponent<AudioSource2DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(AudioSource2DComponent).hash_code()), treeNodeFlags, "Audio Source 2D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Audio Source 2D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& audioComp = entity.GetComponent<AudioSource2DComponent>();

				std::string soundName = audioComp.SoundAsset ? audioComp.SoundAsset->GetName() : "None";
				ImGui::Text("Source: %s", soundName.c_str());

				/// Handle drag and drop for the audio asset
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserAudio))
					{
						const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
						if (AssetManager::GetAssetType(handle) != AssetType::Sound)
						{
							KBR_EDITOR_ERROR("Asset is not a sound: {0}", handle);
							m_NotificationManager.AddNotification("Asset is not a sound", Notification::Type::Error);
							return;
						}
						audioComp.SoundAsset = AssetManager::GetAsset<Sound>(handle);
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::Separator();

				ImGui::Checkbox("Loop", &audioComp.Loop);

				ImGui::DragFloat("Volume", &audioComp.Volume, 0.01f, 0.0f, 1.0f);

				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<AudioSource2DComponent>();
			}
		}
		if (entity.HasComponent<AudioSource3DComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(AudioSource3DComponent).hash_code()), treeNodeFlags, "Audio Source 3D");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Audio Source 3D Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& audioComp = entity.GetComponent<AudioSource3DComponent>();

				std::string soundName = audioComp.SoundAsset ? audioComp.SoundAsset->GetName() : "None";
				ImGui::Text("Source: %s", soundName.c_str());

				/// Handle drag and drop for the audio asset
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserAudio))
					{
						const AssetHandle handle = *static_cast<AssetHandle*>(payload->Data);
						if (AssetManager::GetAssetType(handle) != AssetType::Sound)
						{
							KBR_EDITOR_ERROR("Asset is not a sound: {0}", handle);
							m_NotificationManager.AddNotification("Asset is not a sound", Notification::Type::Error);
							return;
						}
						audioComp.SoundAsset = AssetManager::GetAsset<Sound>(handle);
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::Separator();

				ImGui::Checkbox("Loop", &audioComp.Loop);

				ImGui::DragFloat("Volume", &audioComp.Volume, 0.01f, 0.0f, 1.0f);

				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<AudioSource3DComponent>();
			}
		}
		if (entity.HasComponent<AudioListenerComponent>())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
			const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(AudioListenerComponent).hash_code()), treeNodeFlags, "Audio Listener");
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
			if (ImGui::Button("+", ImVec2{ 20, 20 }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}
			ImGui::PopStyleVar();
			bool componentDeleted = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				ImGui::Text("Audio Listener Settings");
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component"))
				{
					componentDeleted = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			if (opened)
			{
				auto& listenerCOmp = entity.GetComponent<AudioListenerComponent>();

				ImGui::DragFloat("Volume", &listenerCOmp.Volume, 0.01f, 0.0f, 1.0f);

				ImGui::TreePop();
			}
			if (componentDeleted)
			{
				entity.RemoveComponent<AudioListenerComponent>();
			}
		}
	}
}
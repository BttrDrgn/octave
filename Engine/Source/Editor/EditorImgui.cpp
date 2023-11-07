#if EDITOR

#include "EditorImgui.h"
#include "System/System.h"
#include "Engine.h"
#include "EditorUtils.h"
#include "EditorConstants.h"
#include "Renderer.h"
#include "InputDevices.h"
#include "Log.h"
#include "AssetDir.h"
#include "Grid.h"

#include "Nodes/3D/StaticMesh3d.h"
#include "Nodes/3D/SkeletalMesh3d.h"

#include "Assets/Scene.h"
#include "Assets/SoundWave.h"
#include "Assets/ParticleSystem.h"
#include "Assets/StaticMesh.h"
#include "Assets/SkeletalMesh.h"

#include "Viewport3d.h"
#include "Viewport2d.h"
#include "ActionManager.h"
#include "EditorState.h"

#include <functional>

// TODO: If we ever support an OpenGL backend, gotta change this.
#include "backends/imgui_impl_vulkan.cpp"
#include "Graphics/Vulkan/VulkanContext.h"
#include "Graphics/Vulkan/VulkanUtils.h"

#include "CustomImgui.h"

#if PLATFORM_WINDOWS
#include "backends/imgui_impl_win32.cpp"
#elif PLATFORM_LINUX
#error Get the linux backend working!
#endif

struct FileBrowserDirEntry
{
    std::string mName;
    bool mFolder = false;
};

static const float kSidePaneWidth = 200.0f;
static const float kViewportBarHeight = 32.0f;
static const ImGuiWindowFlags kPaneWindowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;

static const ImVec4 kSelectedColor = ImVec4(0.12f, 0.50f, 0.47f, 1.00f);
static const ImVec4 kBgInactive = ImVec4(0.20f, 0.20f, 0.68f, 1.00f);
static const ImVec4 kBgHover = ImVec4(0.26f, 0.61f, 0.98f, 0.80f);

constexpr const uint32_t kPopupInputBufferSize = 256;
static char sPopupInputBuffer[kPopupInputBufferSize] = {};

static bool sNodesDiscovered = false;
static std::vector<std::string> sNode3dNames;
static std::vector<std::string> sNodeWidgetNames;
static std::vector<std::string> sNodeOtherNames;

static ImTextureID sInspectTexId = 0;
static Texture* sPrevInspectTexture = nullptr;

static bool sFileBrowserOpen = false;
static bool sFileBrowserFolderMode = false;
static FileBrowserCallbackFP sFileBrowserCallback = nullptr;
static std::string sFileBrowserPath;
static std::string sFileBrowserCurDir;
static std::vector<FileBrowserDirEntry> sFileBrowserEntries;

static void PopulateFileBrowserDirs()
{
    sFileBrowserEntries.clear();

    DirEntry dirEntry = { };

    SYS_OpenDirectory(sFileBrowserCurDir, dirEntry);

    while (dirEntry.mValid)
    {
        FileBrowserDirEntry entry;
        entry.mName = dirEntry.mFilename;
        entry.mFolder = dirEntry.mDirectory;
        sFileBrowserEntries.push_back(entry);

        SYS_IterateDirectory(dirEntry);
    }

    SYS_CloseDirectory(dirEntry);
}

void EditorOpenFileBrowser(FileBrowserCallbackFP callback, bool folderMode)
{
    if (!sFileBrowserOpen)
    {
        sFileBrowserOpen = true;
        sFileBrowserFolderMode = folderMode;
        sFileBrowserCallback = callback;
        sFileBrowserPath = "";

        if (sFileBrowserCurDir == "")
        {
            if (GetEngineState()->mProjectDirectory != "")
            {
                sFileBrowserCurDir = GetEngineState()->mProjectDirectory;
            }
            else
            {
                sFileBrowserCurDir = "./";
            }
        }

        sFileBrowserCurDir = SYS_GetAbsolutePath(sFileBrowserCurDir);

        PopulateFileBrowserDirs();

        if (sFileBrowserEntries.size() == 0)
        {
            LogWarning("No directory entries found. Reseting to working dir.");
            sFileBrowserCurDir = "./";
            PopulateFileBrowserDirs();

            if (sFileBrowserEntries.size() == 0)
            {
                LogError("Still couldn't find directory entries...");
            }
        }
    }
    else
    {
        LogWarning("Failed to open file browser. It is already open.");
    }
}

static void DrawFileBrowser()
{
    if (sFileBrowserOpen)
    {
        ImGui::OpenPopup("File Browser");
    }

    // Center the modal
    if (ImGui::IsPopupOpen("File Browser"))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(465, 465));
    }

    if (ImGui::BeginPopupModal("File Browser", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        // Text that shows current directory.
        ImGui::Text(sFileBrowserCurDir.c_str());

        // List of selectables that let you navigate/open/delete.
        {
            ImGuiWindowFlags childFlags = ImGuiWindowFlags_None;
            ImGui::BeginChild("File List", ImVec2(450, 350), true, childFlags);

            bool changedDir = false;

            // Show folders
            for (uint32_t i = 0; i < sFileBrowserEntries.size(); ++i)
            {
                if (sFileBrowserEntries[i].mFolder &&
                    sFileBrowserEntries[i].mName != ".")
                {
                    if (ImGui::Selectable(sFileBrowserEntries[i].mName.c_str(), true))
                    {
                        // If a folder is selected, then we need to switch to that directory,
                        // and also set the sFileBrowserPath if in folder mode.
                        sFileBrowserCurDir = SYS_GetAbsolutePath(sFileBrowserCurDir + sFileBrowserEntries[i].mName + "/");
                        changedDir = true;

                        if (sFileBrowserFolderMode)
                        {
                            sFileBrowserPath = sFileBrowserCurDir;
                        }
                    }
                }
            }

            // Show files
            for (uint32_t i = 0; i < sFileBrowserEntries.size(); ++i)
            {
                if (!sFileBrowserEntries[i].mFolder)
                {
                    if (ImGui::Selectable(sFileBrowserEntries[i].mName.c_str()))
                    {
                        if (!sFileBrowserFolderMode)
                        {
                            sFileBrowserPath = sFileBrowserCurDir + "/" + sFileBrowserEntries[i].mName;
                        }
                    }
                }
            }

            ImGui::EndChild();

            if (changedDir)
            {
                PopulateFileBrowserDirs();
            }
        }

        std::string fileFolderName = sFileBrowserPath;
        if (sFileBrowserFolderMode && 
            fileFolderName.size() > 0 &&
            (fileFolderName.back() == '/' || fileFolderName.back() == '\\'))
        {
            fileFolderName.pop_back();
        }

        size_t lastSlashIdx = fileFolderName.find_last_of("/\\");
        if (lastSlashIdx != std::string::npos)
        {
            fileFolderName = fileFolderName.substr(lastSlashIdx + 1);
        }

        ImGui::SetNextItemWidth(400);
        if (ImGui::InputText(sFileBrowserFolderMode ? "Folder" : "File", &fileFolderName))
        {
            sFileBrowserPath = sFileBrowserCurDir + fileFolderName;

            // Add the trailing slash if its a folder.
            if (sFileBrowserFolderMode &&
                sFileBrowserPath.size() > 0 &&
                (sFileBrowserPath.back() != '/' && sFileBrowserPath.back() != '\\'))
            {
#if PLATFORM_WINDOWS
                sFileBrowserPath.push_back('\\');
#else
                sFileBrowserPath.push_back('/');
#endif
            }
        }

        ImGui::Text(sFileBrowserPath.c_str());

        if (ImGui::Button("Open"))
        {
            if (sFileBrowserCallback != nullptr)
            {
                sFileBrowserCallback(sFileBrowserPath);
            }

            sFileBrowserOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            sFileBrowserOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void DiscoverNodeClasses()
{
    sNode3dNames.clear();
    sNodeWidgetNames.clear();

    const std::vector<Factory*>& nodeFactories = Node::GetFactoryList();
    for (uint32_t i = 0; i < nodeFactories.size(); ++i)
    {
        Node* node = Node::CreateInstance(nodeFactories[i]->GetType());
        if (node->As<Node3D>())
        {
            if (strcmp(node->GetClassName(), "Node3D") == 0)
            {
                sNode3dNames.insert(sNode3dNames.begin(), node->GetClassName());
            }
            else
            {
                sNode3dNames.push_back(nodeFactories[i]->GetClassName());
            }
        }
        else if (node->As<Widget>())
        {
            if (strcmp(node->GetClassName(), "Widget") == 0)
            {
                sNodeWidgetNames.insert(sNodeWidgetNames.begin(), node->GetClassName());
            }
            else
            {
                sNodeWidgetNames.push_back(nodeFactories[i]->GetClassName());
            }
        }
        else if (strcmp(node->GetClassName(), "Node") != 0)
        {
            sNodeOtherNames.push_back(nodeFactories[i]->GetClassName());
        }

        delete node;
    }
}

static void CreateNewAsset(TypeId assetType, const char* assetName)
{
    AssetStub* stub = nullptr;
    AssetDir* currentDir = GetEditorState()->GetAssetDirectory();

    if (currentDir == nullptr)
        return;

    if (assetType == Material::GetStaticType())
    {
        stub = EditorAddUniqueAsset(assetName, currentDir, Material::GetStaticType(), true);
        Asset* selAsset = GetEditorState()->GetSelectedAsset();


        if (stub != nullptr &&
            stub->mAsset != nullptr &&
            selAsset != nullptr &&
            selAsset->GetType() == Texture::GetStaticType())
        {
            Material* material = stub->mAsset->As<Material>();
            Texture* texture = selAsset->As<Texture>();

            // Auto assign the selected texture to Texture_0
            material->SetTexture(TEXTURE_0, texture);

            std::string newMatName = texture->GetName();

            if (newMatName.length() >= 2 && newMatName[0] == 'T' && newMatName[1] == '_')
            {
                newMatName[0] = 'M';
            }
            else
            {
                newMatName = std::string("M_") + newMatName;
            }

            AssetManager::Get()->RenameAsset(material, newMatName);
        }
    }
    else if (assetType == ParticleSystem::GetStaticType())
    {
        stub = EditorAddUniqueAsset(assetName, currentDir, ParticleSystem::GetStaticType(), true);
    }
    else if (assetType == Scene::GetStaticType())
    {
        stub = EditorAddUniqueAsset(assetName, currentDir, Scene::GetStaticType(), true);
    }

    if (stub != nullptr)
    {
        AssetManager::Get()->SaveAsset(*stub);
    }
}

static void AssignAssetToProperty(RTTI* owner, PropertyOwnerType ownerType, Property& prop, uint32_t index, Asset* newAsset)
{
    if (newAsset != nullptr &&
        newAsset != prop.GetAsset() &&
        (prop.mExtra == 0 || newAsset->GetType() == TypeId(prop.mExtra)))
    {
        ActionManager::Get()->EXE_EditProperty(owner, ownerType, prop.mName, index, newAsset);
    }
}

static void DrawPropertyList(RTTI* owner, std::vector<Property>& props)
{
    ActionManager* am = ActionManager::Get();
    const float kIndentWidth = 0.0f;
    const bool ctrlDown = IsControlDown();
    const bool altDown = IsAltDown();
    const bool shiftDown = IsShiftDown();

    PropertyOwnerType ownerType = PropertyOwnerType::Global;
    if (owner != nullptr)
    {
        if (owner->As<Node>())
        {
            ownerType = PropertyOwnerType::Node;
        }
        else if (owner->As<Asset>())
        {
            ownerType = PropertyOwnerType::Asset;
        }
    }

    for (uint32_t p = 0; p < props.size(); ++p)
    {
        ImGui::PushID(p);

        Property& prop = props[p];
        DatumType propType = prop.GetType();

        // Bools handle name on same line after checkbox
        if (propType != DatumType::Bool || prop.GetCount() > 1)
        {
            ImGui::Text(prop.mName.c_str());

            if (kIndentWidth > 0.0f)
            {
                ImGui::Indent(kIndentWidth);
            }

            if (prop.IsVector())
            {
                ImGui::SameLine();
                if (ImGui::Button("+"))
                {
                    if (prop.IsExternal())
                    {
                        prop.PushBackVector();
                    }
                    else
                    {
                        prop.SetCount(prop.GetCount() + 1);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("-"))
                {
                    if (prop.GetCount() > 0)
                    {
                        if (prop.IsExternal())
                        {
                            prop.EraseVector(prop.GetCount() - 1);
                        }
                        else
                        {
                            prop.Erase(prop.GetCount() - 1);
                        }
                    }
                }
            }
        }

        for (uint32_t i = 0; i < prop.GetCount(); ++i)
        {
            ImGui::PushID(i);
            switch (propType)
            {
            case DatumType::Integer:
            {
                static int32_t sOrigVal = 0;
                int32_t propVal = prop.GetInteger(i);
                int32_t preVal = propVal;

                if (prop.mEnumCount > 0)
                {
                    if (ImGui::Combo("", &propVal, prop.mEnumStrings, prop.mEnumCount))
                    {
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                    }
                }
                else
                {
                    ImGui::DragInt("", &propVal);

                    if (ImGui::IsItemActivated())
                    {
                        sOrigVal = preVal;
                    }

                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        prop.SetInteger(sOrigVal);
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                    }
                    else if (propVal != preVal)
                    {
                        prop.SetInteger(propVal, i);
                    }
                }
                break;
            }
            case DatumType::Float:
            {
                static float sOrigVal = 0.0f;
                float propVal = prop.GetFloat(i);
                float preVal = propVal;

                ImGui::DragFloat("", &propVal);

                if (ImGui::IsItemActivated())
                {
                    sOrigVal = preVal;
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    prop.SetFloat(sOrigVal);
                    am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                }
                else if (propVal != preVal)
                {
                    prop.SetFloat(propVal, i);
                }
                break;
            }
            case DatumType::Bool:
            {
                bool propVal = prop.GetBool(i);
                if (ImGui::Checkbox("", &propVal))
                {
                    am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                }

                ImGui::SameLine();
                ImGui::Text(prop.mName.c_str());
                break;
            }
            case DatumType::String:
            {
                static std::string sTempString;
                static std::string sOrigVal;
                sTempString = prop.GetString(i);

                ImGui::InputText("", &sTempString);

                if (ImGui::IsItemActivated())
                {
                    sOrigVal = sTempString;
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    if (sTempString != sOrigVal)
                    {
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, sTempString);
                    }
                }
                break;
            }
            case DatumType::Vector2D:
            {
                static glm::vec2 sOrigVal = {};
                glm::vec2 propVal = prop.GetVector2D(i);
                glm::vec2 preVal = propVal;

                ImGui::DragFloat2("", &propVal[0], 1.0f, 0.0f, 0.0f, "%.2f");

                if (ImGui::IsItemActivated())
                {
                    sOrigVal = preVal;
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    prop.SetVector2D(sOrigVal);
                    am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                }
                else if (propVal != preVal)
                {
                    prop.SetVector2D(propVal, i);
                }
                break;
            }
            case DatumType::Vector:
            {
                static glm::vec3 sOrigVal = {};
                glm::vec3 propVal = prop.GetVector(i);
                glm::vec3 preVal = propVal;

                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.85f);
                //ImGui::DragFloat3("", &propVal[0], 1.0f, 0.0f, 0.0f, "%.2f");
                float vMin = 0.0f;
                float vMax = 0.0f;
                ImGui::OctDragScalarN("", ImGuiDataType_Float, &propVal[0], 3, 1.0f, &vMin, &vMax, "%.2f", 0);
                ImGui::PopItemWidth();

                if (ImGui::IsItemActivated())
                {
                    sOrigVal = preVal;
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    prop.SetVector(sOrigVal);
                    am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                }
                else if (propVal != preVal)
                {
                    prop.SetVector(propVal, i);
                }
                break;
            }
            case DatumType::Color:
            {
                static glm::vec4 sOrigVal = {};
                glm::vec4 propVal = prop.GetColor(i);
                glm::vec4 preVal = propVal;

                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.85f);
                ImGui::OctColorEdit4("", &propVal[0], 0);
                ImGui::PopItemWidth();

                if (ImGui::IsItemActivated())
                {
                    sOrigVal = preVal;
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    prop.SetColor(sOrigVal);
                    am->EXE_EditProperty(owner, ownerType, prop.mName, i, propVal);
                }
                else if (propVal != preVal)
                {
                    prop.SetColor(propVal, i);
                }
                break;
            }
            case DatumType::Asset:
            {
                Asset* propVal = prop.GetAsset(i);

                bool useAssetColor = (prop.mExtra != 0);
                if (useAssetColor)
                {
                    glm::vec4 assetColor = AssetManager::Get()->GetEditorAssetColor((TypeId)prop.mExtra);
                    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(assetColor.r, assetColor.g, assetColor.b, assetColor.a));
                }

                if (ctrlDown)
                {
                    if (ImGui::Button("<<") && propVal)
                    {
                        GetEditorState()->BrowseToAsset(propVal->GetName());
                    }
                }
                else if (altDown)
                {
                    if (ImGui::Button("^^") && propVal)
                    {
                        GetEditorState()->InspectObject(propVal);
                    }
                }
                else
                {
                    if (ImGui::Button(">>"))
                    {
                        Asset* selAsset = GetEditorState()->GetSelectedAsset();
                        if (selAsset != nullptr)
                        {
                            AssignAssetToProperty(owner, ownerType, prop, i, selAsset);
                        }
                    }

                    if (propVal != nullptr && 
                        ImGui::IsItemHovered() && 
                        IsKeyJustDown(KEY_DELETE))
                    {
                        ActionManager::Get()->EXE_EditProperty(owner, ownerType, prop.mName, i, (Asset*) nullptr);

                    }
                }

                ImGui::SameLine();

                static std::string sTempString;
                static std::string sOrigVal;
                sTempString = propVal ? propVal->GetName() : "";

                ImGui::InputText("", &sTempString);

                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    if (sTempString == "null" ||
                        sTempString == "NULL" ||
                        sTempString == "Null")
                    {
                        Asset* nullAsset = nullptr;
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, nullAsset);
                    }
                    else
                    {
                        Asset* newAsset =  LoadAsset(sTempString);
                        AssignAssetToProperty(owner, ownerType, prop, i, newAsset);
                    }
                }

                if (useAssetColor)
                {
                    ImGui::PopStyleColor();
                }

                break;
            }
            case DatumType::Byte:
            {
                static int32_t sOrigVal = 0;
                int32_t propVal = prop.GetByte(i);
                int32_t preVal = propVal;

                if (prop.mEnumCount > 0)
                {
                    if (ImGui::Combo("", &propVal, prop.mEnumStrings, prop.mEnumCount))
                    {
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, (uint8_t)propVal);
                    }
                }
                else if (prop.mExtra == int32_t(ByteExtra::FlagWidget) ||
                    prop.mExtra == int32_t(ByteExtra::ExclusiveFlagWidget)) // Should these be bitwise checks?
                {
                    ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
                    itemSpacing.x = 2.0f;
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, itemSpacing);

                    for (uint32_t f = 0; f < 8; ++f)
                    {
                        if (f > 0)
                            ImGui::SameLine();

                        ImGui::PushID(f);

                        int32_t bit = 7 - int32_t(f);
                        bool bitSet = (propVal >> bit) & 1;

                        ImVec4* imColors = ImGui::GetStyle().Colors;

                        if (bitSet)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, kSelectedColor);
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, imColors[ImGuiCol_Button]);
                        }

                        if (ImGui::Button("", ImVec2(16.0f, 16.0f)))
                        {
                            bitSet = !bitSet;
                            uint8_t newBitMask = propVal;
                            if (bitSet)
                            {
                                newBitMask = newBitMask | (1 << bit);
                            }
                            else
                            {
                                newBitMask = newBitMask & (~(1 << bit));
                            }

                            propVal = newBitMask;

                            am->EXE_EditProperty(owner, ownerType, prop.mName, i, uint8_t(propVal));
                        }

                        ImGui::PopStyleColor();

                        ImGui::PopID();
                    }

                    ImGui::PopStyleVar();
                }
                else
                {
                    ImGui::DragInt("", &propVal);

                    if (ImGui::IsItemActivated())
                    {
                        sOrigVal = preVal;
                    }

                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        prop.SetByte((uint8_t)sOrigVal);
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, uint8_t(propVal));
                    }
                    else if (propVal != preVal)
                    {
                        prop.SetByte((uint8_t)propVal, i);
                    }
                }
                break;
            }
            case DatumType::Short:
            {
                static int32_t sOrigVal = 0;
                int32_t propVal = prop.GetShort(i);
                int32_t preVal = propVal;

                if (prop.mEnumCount > 0)
                {
                    if (ImGui::Combo("", &propVal, prop.mEnumStrings, prop.mEnumCount))
                    {
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, (int16_t)propVal);
                    }
                }
                else
                {
                    ImGui::DragInt("", &propVal);

                    if (ImGui::IsItemActivated())
                    {
                        sOrigVal = preVal;
                    }

                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        prop.SetShort((int16_t)sOrigVal);
                        am->EXE_EditProperty(owner, ownerType, prop.mName, i, int16_t(propVal));
                    }
                    else if (propVal != preVal)
                    {
                        prop.SetShort((int16_t)propVal, i);
                    }
                }
                break;
            }
            default: break;
            }

            ImGui::PopID();
        }

        if (propType != DatumType::Bool)
        {
            if (kIndentWidth > 0.0f)
            {
                ImGui::Unindent(kIndentWidth);
            }
        }

        ImGui::PopID();
    }
}

static void DrawAddNodeMenu(Node* node)
{
    ActionManager* am = ActionManager::Get();

    if (!sNodesDiscovered)
    {
        DiscoverNodeClasses();
        sNodesDiscovered = true;
    }

    if (ImGui::MenuItem("Node"))
    {
        Node* newNode = am->EXE_SpawnNode(Node::GetStaticType());
        if (node)
        {
            node->AddChild(newNode);
        }
        else
        {
            GetWorld()->PlaceNewlySpawnedNode(newNode);
        }
        
        GetEditorState()->SetSelectedNode(newNode);
    }

    if (ImGui::BeginMenu("3D"))
    {
        for (uint32_t i = 0; i < sNode3dNames.size(); ++i)
        {
            if (ImGui::MenuItem(sNode3dNames[i].c_str()))
            {
                const char* nodeName = sNode3dNames[i].c_str();

                Node* newNode = am->EXE_SpawnNode(nodeName);

                if (node)
                    node->AddChild(newNode);
                else
                    GetWorld()->PlaceNewlySpawnedNode(newNode);

                GetEditorState()->SetSelectedNode(newNode);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Widget"))
    {
        for (uint32_t i = 0; i < sNodeWidgetNames.size(); ++i)
        {
            if (ImGui::MenuItem(sNodeWidgetNames[i].c_str()))
            {
                const char* nodeName = sNodeWidgetNames[i].c_str();

                Node* newNode = am->EXE_SpawnNode(nodeName);

                if (node)
                    node->AddChild(newNode);
                else
                    GetWorld()->PlaceNewlySpawnedNode(newNode);

                GetEditorState()->SetSelectedNode(newNode);
            }
        }

        ImGui::EndMenu();
    }

    if (sNodeOtherNames.size() > 0 &&
        ImGui::BeginMenu("Other"))
    {
        for (uint32_t i = 0; i < sNodeOtherNames.size(); ++i)
        {
            if (ImGui::MenuItem(sNodeOtherNames[i].c_str()))
            {
                const char* nodeName = sNodeOtherNames[i].c_str();
                Node* newNode = am->EXE_SpawnNode(nodeName);

                if (node)
                    node->AddChild(newNode);
                else
                    GetWorld()->PlaceNewlySpawnedNode(newNode);

                GetEditorState()->SetSelectedNode(newNode);
            }
        }

        ImGui::EndMenu();
    }
}

static void DrawSpawnBasic3dMenu(Node* node, bool setFocusPos)
{
    ActionManager* am = ActionManager::Get();
    glm::vec3 spawnPos = EditorGetFocusPosition();
    Asset* selAsset = GetEditorState()->GetSelectedAsset();

    if (ImGui::MenuItem(BASIC_NODE_3D))
        am->SpawnBasicNode(BASIC_NODE_3D, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_STATIC_MESH))
        am->SpawnBasicNode(BASIC_STATIC_MESH, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_SKELETAL_MESH))
        am->SpawnBasicNode(BASIC_SKELETAL_MESH, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_POINT_LIGHT))
        am->SpawnBasicNode(BASIC_POINT_LIGHT, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_DIRECTIONAL_LIGHT))
        am->SpawnBasicNode(BASIC_DIRECTIONAL_LIGHT, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_PARTICLE))
        am->SpawnBasicNode(BASIC_PARTICLE, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_AUDIO))
        am->SpawnBasicNode(BASIC_AUDIO, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_BOX))
        am->SpawnBasicNode(BASIC_BOX, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_SPHERE))
        am->SpawnBasicNode(BASIC_SPHERE, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_CAPSULE))
        am->SpawnBasicNode(BASIC_CAPSULE, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_SCENE))
        am->SpawnBasicNode(BASIC_SCENE, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_CAMERA))
        am->SpawnBasicNode(BASIC_CAMERA, node, selAsset, setFocusPos, spawnPos);
    if (ImGui::MenuItem(BASIC_TEXT_MESH))
        am->SpawnBasicNode(BASIC_TEXT_MESH, node, selAsset, setFocusPos, spawnPos);
}
static void DrawSpawnBasicWidgetMenu(Node* node)
{
    ActionManager* am = ActionManager::Get();
    Asset* selAsset = GetEditorState()->GetSelectedAsset();

    const char* widgetTypeName = nullptr;

    if (ImGui::MenuItem("Widget"))
        widgetTypeName = "Widget";
    if (ImGui::MenuItem("Quad"))
        widgetTypeName = "Quad";
    if (ImGui::MenuItem("Text"))
        widgetTypeName = "Text";

    if (widgetTypeName != nullptr)
    {
        Node* newWidget = am->EXE_SpawnNode(widgetTypeName);

        if (node == nullptr)
        {
            GetWorld()->PlaceNewlySpawnedNode(newWidget);
        }
        else
        {
            node->AddChild(newWidget);
        }

        OCT_ASSERT(newWidget);
        if (newWidget)
        {
            GetEditorState()->SetSelectedNode(newWidget);
        }
    }
}

static void DrawPackageMenu()
{
    ActionManager* am = ActionManager::Get();

    //if (ImGui::BeginPopup("PackagePopup"))
    //{
#if PLATFORM_WINDOWS
    if (ImGui::MenuItem("Windows"))
        am->BuildData(Platform::Windows, false);
#elif PLATFORM_LINUX
    if (ImGui::MenuItem("Linux"))
        am->BuildData(Platform::Linux, false);
#endif
    if (ImGui::MenuItem("Android"))
        am->BuildData(Platform::Android, false);
    if (ImGui::MenuItem("GameCube"))
        am->BuildData(Platform::GameCube, false);
    if (ImGui::MenuItem("Wii"))
        am->BuildData(Platform::Wii, false);
    if (ImGui::MenuItem("3DS"))
        am->BuildData(Platform::N3DS, false);
    if (ImGui::MenuItem("GameCube Embedded"))
        am->BuildData(Platform::GameCube, true);
    if (ImGui::MenuItem("Wii Embedded"))
        am->BuildData(Platform::Wii, true);
    if (ImGui::MenuItem("3DS Embedded"))
        am->BuildData(Platform::N3DS, true);

    //    ImGui::EndPopup();
    //}
}

static void DrawScenePanel()
{
    ActionManager* am = ActionManager::Get();

    const float halfHeight = (float)GetEngineState()->mWindowHeight / 2.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(kSidePaneWidth, halfHeight));

    ImGui::Begin("Scene", nullptr, kPaneWindowFlags);

    ImGuiTreeNodeFlags treeNodeFlags =
        ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_DefaultOpen;

    World* world = GetWorld();
    Node* rootNode = world ? world->GetRootNode() : nullptr;
    bool sNodeContextActive = false;

    glm::vec4 sceneColor = AssetManager::Get()->GetEditorAssetColor(Scene::GetStaticType());
    ImVec4 sceneColorIm = ImVec4(sceneColor.r, sceneColor.g, sceneColor.b, sceneColor.a);

    std::function<void(Node*)> drawTree = [&](Node* node)
    {
        bool nodeSelected = GetEditorState()->IsNodeSelected(node);
        bool nodeSceneLinked = (node->GetScene() != nullptr && node != rootNode);

        ImGuiTreeNodeFlags nodeFlags = treeNodeFlags;
        if (nodeSelected)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        if (node->GetNumChildren() == 0 || nodeSceneLinked)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        if (nodeSceneLinked)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, sceneColorIm);
        }

        bool nodeOpen = ImGui::TreeNodeEx(node->GetName().c_str(), nodeFlags);
        bool nodeClicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();

        if (nodeSceneLinked)
        {
            ImGui::PopStyleColor();
        }

        if (nodeSelected && GetEditorState()->mTrackSelectedNode)
        {
            ImGui::SetScrollHereY(0.5f);
            GetEditorState()->mTrackSelectedNode = false;
        }

        if (ImGui::BeginPopupContextItem())
        {
            bool setTextInputFocus = false;

            sNodeContextActive = true;

            if (node->IsSceneLinked() && ImGui::Selectable("Open Scene"))
            {
                GetEditorState()->OpenEditScene(node->GetScene());
            }
            if (node->GetParent() != nullptr &&
                ImGui::BeginMenu("Move"))
            {
                Node* parent = node->GetParent();
                int32_t childSlot = parent->FindChildIndex(node);

                if (ImGui::Selectable("Top"))
                    am->EXE_AttachNode(node, parent, 0, -1);
                if (ImGui::Selectable("Up"))
                    am->EXE_AttachNode(node, parent, glm::max<int32_t>(childSlot - 1, 0), -1);
                if (ImGui::Selectable("Down"))
                    am->EXE_AttachNode(node, parent, childSlot + 1, -1);
                if (ImGui::Selectable("Bottom"))
                    am->EXE_AttachNode(node, parent, -1, -1);

                ImGui::EndMenu();
            }
            if (ImGui::Selectable("Rename", false, ImGuiSelectableFlags_DontClosePopups))
            {
                ImGui::OpenPopup("Rename Node");
                strncpy(sPopupInputBuffer, node->GetName().c_str(), kPopupInputBufferSize);
                setTextInputFocus = true;
            }
            if (ImGui::Selectable("Duplicate"))
            {
                am->DuplicateNodes({ node });
            }
            if (!nodeSceneLinked && ImGui::Selectable("Attach Selected"))
            {
                am->AttachSelectedNodes(node, -1);
            }
            if (!nodeSceneLinked && node->As<SkeletalMesh3D>())
            {
                if (ImGui::Selectable("Attach Selected To Bone"))
                {
                    ImGui::OpenPopup("Attach Selected To Bone");
                    setTextInputFocus = true;
                }
            }
            if (ImGui::Selectable("Set Root Node"))
            {
                am->EXE_SetRootNode(node);
            }
            if (nodeSceneLinked && ImGui::Selectable("Unlink Scene"))
            {
                am->EXE_UnlinkScene(node);
            }
            if (ImGui::Selectable("Delete"))
            {
                am->EXE_DeleteNode(node);
            }
            if (node->As<StaticMesh3D>() &&
                ImGui::Selectable("Merge"))
            {
                LogDebug("TODO: Implement Merge for static meshes.");
            }
            if (!nodeSceneLinked && ImGui::BeginMenu("Add Node"))
            {
                DrawAddNodeMenu(node);
                ImGui::EndMenu();
            }
            if (!nodeSceneLinked && ImGui::BeginMenu("Add Basic 3D"))
            {
                DrawSpawnBasic3dMenu(node, false);
                ImGui::EndMenu();
            }
            if (!nodeSceneLinked && ImGui::BeginMenu("Add Basic Widget"))
            {
                DrawSpawnBasicWidgetMenu(node);
                ImGui::EndMenu();
            }
            //if (ImGui::Selectable("Add Scene..."))
            //{

            //}

            // Sub Popups

            if (ImGui::BeginPopup("Rename Node"))
            {
                if (setTextInputFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                }

                if (ImGui::InputText("Node Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    node->SetName(sPopupInputBuffer);
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Attach Selected To Bone"))
            {
                if (setTextInputFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                }

                if (ImGui::InputText("Bone Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    SkeletalMesh3D* skNode = node->As<SkeletalMesh3D>();
                    if (skNode)
                    {
                        int32_t boneIdx = skNode->FindBoneIndex(sPopupInputBuffer);
                        am->AttachSelectedNodes(skNode, boneIdx);
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            if (!nodeSceneLinked)
            {
                for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
                {
                    Node* child = node->GetChild(i);
                    drawTree(child);
                }
            }

            ImGui::TreePop();
        }

        if (nodeClicked)
        {
            if (nodeSelected)
            {
                GetEditorState()->DeselectNode(node);
            }
            else
            {
                if (ImGui::GetIO().KeyCtrl)
                {
                    GetEditorState()->AddSelectedNode(node, false);
                }
                else
                {
                    GetEditorState()->SetSelectedNode(node);
                }
            }
        }

    };

    if (rootNode != nullptr)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 6.0f);
        drawTree(rootNode);
        ImGui::PopStyleVar();
    }

    // If no popup is open and we aren't inputting text...
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
        ImGui::IsWindowHovered() &&
        !ImGui::GetIO().WantTextInput && 
        !sNodeContextActive)
    {
        const bool ctrlDown = IsControlDown();
        const bool shiftDown = IsShiftDown();
        const bool altDown = IsAltDown();

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("Null Node Context");
        }

        const std::vector<Node*>& selNodes = GetEditorState()->GetSelectedNodes();

        // Move Up/Down selected node.
        if (selNodes.size() == 1 &&
            selNodes[0]->GetParent() != nullptr)
        {
            Node* node = selNodes[0];
            Node* parent = node->GetParent();
            int32_t childIndex = parent->FindChildIndex(node);

            if (IsKeyJustDown(KEY_MINUS))
            {
                am->EXE_AttachNode(node, parent, glm::max<int32_t>(childIndex - 1, 0), -1);
            }
            else if (IsKeyJustDown(KEY_PLUS))
            {
                am->EXE_AttachNode(node, parent, childIndex + 1, -1);
            }
        }

        if (selNodes.size() > 0)
        {
            if (IsKeyJustDown(KEY_DELETE))
            {
                am->EXE_DeleteNodes(selNodes);
            }
            else if (ctrlDown && IsKeyJustDown(KEY_D))
            {
                am->DuplicateNodes(selNodes);
            }
        }

    }

    if (ImGui::BeginPopup("Null Node Context"))
    {
        if (ImGui::BeginMenu("Spawn Node"))
        {
            DrawAddNodeMenu(nullptr);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Spawn Basic 3D"))
        {
            DrawSpawnBasic3dMenu(nullptr, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Spawn Basic Widget"))
        {
            DrawSpawnBasicWidgetMenu(nullptr);
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

static void DrawAssetsContextPopup(AssetStub* stub, AssetDir* dir)
{
    bool setTextInputFocus = false;
    bool closeContextPopup = false;
    static TypeId sNewAssetType = INVALID_TYPE_ID;

    ActionManager* actMan = ActionManager::Get();
    AssetManager* assMan = AssetManager::Get();

    AssetDir* curDir = GetEditorState()->GetAssetDirectory();

    bool engineFile = false;
    if ((stub && stub->mEngineAsset) ||
        (dir && dir->mEngineDir))
    {
        engineFile = true;
    }

    bool canInstantiate = false;
    if (stub &&
        (stub->mType == Scene::GetStaticType() ||
            stub->mType == SoundWave::GetStaticType() ||
            stub->mType == StaticMesh::GetStaticType() ||
            stub->mType == SkeletalMesh::GetStaticType() ||
            stub->mType == ParticleSystem::GetStaticType()))
    {
        canInstantiate = true;
    }

    if (stub && ImGui::Selectable("Properties"))
    {
        if (stub->mAsset == nullptr)
            AssetManager::Get()->LoadAsset(*stub);

        GetEditorState()->InspectObject(stub->mAsset);
    }

    if (stub && stub->mType == Scene::GetStaticType())
    {
        if (ImGui::Selectable("Open Scene"))
        {
            if (stub->mAsset == nullptr)
                AssetManager::Get()->LoadAsset(*stub);

            Scene* scene = stub->mAsset ? stub->mAsset->As<Scene>() : nullptr;
            if (scene != nullptr)
            {
                GetEditorState()->OpenEditScene(scene);
            }
            else
            {
                LogError("Failed to load scene asset?");
            }
        }

        if (ImGui::Selectable("Set Startup Scene"))
        {
            GetEditorState()->mStartupSceneName = stub->mName;
            GetEditorState()->WriteEditorSave();
        }
    }

    if (canInstantiate && ImGui::Selectable("Instantiate"))
    {
        if (stub->mAsset == nullptr)
            assMan->LoadAsset(*stub);

        if (stub->mAsset != nullptr)
        {
            Asset* srcAsset = stub->mAsset;
            glm::vec3 spawnPos = EditorGetFocusPosition();
            Node* selNode = GetEditorState()->GetSelectedNode();

            if (stub->mType == Scene::GetStaticType())
                actMan->SpawnBasicNode(BASIC_SCENE, selNode, srcAsset, selNode == nullptr, spawnPos);
            else if (stub->mType == SoundWave::GetStaticType())
                actMan->SpawnBasicNode(BASIC_AUDIO, selNode, srcAsset, selNode == nullptr, spawnPos);
            else if (stub->mType == StaticMesh::GetStaticType())
                actMan->SpawnBasicNode(BASIC_STATIC_MESH, selNode, srcAsset, selNode == nullptr, spawnPos);
            else if (stub->mType == SkeletalMesh::GetStaticType())
                actMan->SpawnBasicNode(BASIC_SKELETAL_MESH, selNode, srcAsset, selNode == nullptr, spawnPos);
            else if (stub->mType == ParticleSystem::GetStaticType())
                actMan->SpawnBasicNode(BASIC_PARTICLE, selNode, srcAsset, selNode == nullptr, spawnPos);
        }
    }


    if (!engineFile && (stub || dir))
    {
        if (stub && ImGui::Selectable("Save"))
        {
            if (stub->mAsset == nullptr)
                AssetManager::Get()->LoadAsset(*stub);

            assMan->SaveAsset(*stub);
        }
        if (ImGui::Selectable("Rename", false, ImGuiSelectableFlags_DontClosePopups))
        {
            ImGui::OpenPopup("Rename Asset");
            strncpy(sPopupInputBuffer, stub ? stub->mName.c_str() : dir->mName.c_str(), kPopupInputBufferSize);
            setTextInputFocus = true;
        }
        if (ImGui::Selectable("Delete"))
        {
            if (stub)
            {
                actMan->DeleteAsset(stub);
            }
            else if (dir)
            {
                actMan->DeleteAssetDir(dir);
                GetEditorState()->ClearAssetDirHistory();
            }

        }

        if (stub && ImGui::Selectable("Duplicate"))
        {
            GetEditorState()->DuplicateAsset(stub);
        }
    }

    if (curDir && !curDir->mEngineDir)
    {
        if (ImGui::Selectable("Import Asset"))
        {
            actMan->ImportAsset();
        }

        if (ImGui::BeginMenu("Create Asset"))
        {
            bool showPopup = false;

            if (ImGui::Selectable("Material", false, ImGuiSelectableFlags_DontClosePopups))
            {
                sNewAssetType = Material::GetStaticType();
                showPopup = true;
            }
            if (ImGui::Selectable("Particle System", false, ImGuiSelectableFlags_DontClosePopups))
            {
                sNewAssetType = ParticleSystem::GetStaticType();
                showPopup = true;
            }
            if (ImGui::Selectable("Scene", false, ImGuiSelectableFlags_DontClosePopups))
            {
                sNewAssetType = Scene::GetStaticType();
                showPopup = true;
            }

            ImGui::EndMenu();

            if (showPopup)
            {
                ImGui::OpenPopup("New Asset Name");
                strncpy(sPopupInputBuffer, "", kPopupInputBufferSize);
                setTextInputFocus = true;
            }
        }

        if (ImGui::Selectable("New Folder", false, ImGuiSelectableFlags_DontClosePopups))
        {
            ImGui::OpenPopup("New Folder");
            strncpy(sPopupInputBuffer, "", kPopupInputBufferSize);
            setTextInputFocus = true;
        }

        if (ImGui::Selectable("Capture Active Scene", false, ImGuiSelectableFlags_DontClosePopups))
        {
            AssetStub* saveStub = nullptr;
            if (stub && stub->mType == Scene::GetStaticType())
            {
                saveStub = stub;
            }

            if (saveStub == nullptr)
            {
                ImGui::OpenPopup("Capture To New Scene");
                strncpy(sPopupInputBuffer, "", kPopupInputBufferSize);
                setTextInputFocus = true;
            }
            else
            {
                GetEditorState()->CaptureAndSaveScene(saveStub, nullptr);
                closeContextPopup = true;
            }
        }

        const std::vector<Node*>& selNodes = GetEditorState()->GetSelectedNodes();
        if (selNodes.size() == 1 && ImGui::Selectable("Capture Selected Node"))
        {
            AssetStub* saveStub = nullptr;
            if (stub && stub->mType == Scene::GetStaticType())
            {
                saveStub = stub;
            }

            GetEditorState()->CaptureAndSaveScene(saveStub, selNodes[0]);
        }
    }

    if (ImGui::BeginPopup("Rename Asset"))
    {
        if (setTextInputFocus)
        {
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputText("Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (stub)
            {
                Asset* asset = AssetManager::Get()->LoadAsset(*stub);
                AssetManager::Get()->RenameAsset(asset, sPopupInputBuffer);
                AssetManager::Get()->SaveAsset(*stub);
            }
            else if (dir)
            {
                AssetManager::Get()->RenameDirectory(dir, sPopupInputBuffer);
            }

            ImGui::CloseCurrentPopup();
            closeContextPopup = true;
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("New Folder"))
    {
        if (setTextInputFocus)
        {
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputText("Folder Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const std::string folderName = sPopupInputBuffer;

            if (folderName != "")
            {
                if (SYS_CreateDirectory((curDir->mPath + folderName).c_str()))
                {
                    curDir->CreateSubdirectory(folderName);
                }
                else
                {
                    LogError("Failed to create folder");
                }
            }

            ImGui::CloseCurrentPopup();
            closeContextPopup = true;
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Capture To New Scene"))
    {
        if (setTextInputFocus)
        {
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputText("Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string sceneName = sPopupInputBuffer;

            if (sceneName == "")
            {
                sceneName = "SC_Scene";
            }

            AssetStub* saveStub = EditorAddUniqueAsset(sceneName.c_str(), curDir, Scene::GetStaticType(), true);
            GetEditorState()->CaptureAndSaveScene(saveStub, nullptr);

            ImGui::CloseCurrentPopup();
            closeContextPopup = true;
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("New Asset Name"))
    {
        if (setTextInputFocus)
        {
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputText("Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            std::string assetName = sPopupInputBuffer;

            if (assetName == "")
            {
                if (sNewAssetType == Material::GetStaticType())
                    assetName = "M_Material";
                else if (sNewAssetType == ParticleSystem::GetStaticType())
                    assetName = "P_Particle";
                else if (sNewAssetType == Scene::GetStaticType())
                    assetName = "SC_Scene";
            }

            if (assetName != "" && sNewAssetType != INVALID_TYPE_ID)
            {
                CreateNewAsset(sNewAssetType, assetName.c_str());
            }

            ImGui::CloseCurrentPopup();
            closeContextPopup = true;
        }

        ImGui::EndPopup();
    }

    if (closeContextPopup)
    {
        ImGui::CloseCurrentPopup();
    }
}

static void DrawAssetBrowser(bool showFilter, bool interactive)
{
    AssetDir* currentDir = GetEditorState()->GetAssetDirectory();

    static std::string sUpperAssetName;
    std::string& filterStr = GetEditorState()->mAssetFilterStr;
    std::vector<AssetStub*>& filteredStubs = GetEditorState()->mFilteredAssetStubs;

    if (showFilter && ImGui::InputText("Filter", &filterStr, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        filteredStubs.clear();

        if (filterStr != "")
        {
            // Convert filter string to all upper case
            std::string filterStrUpper = filterStr;
            for (uint32_t c = 0; c < filterStrUpper.size(); ++c)
            {
                filterStrUpper[c] = toupper(filterStrUpper[c]);
            }

            // Iterate through all matching asset names
            const auto& assetMap = AssetManager::Get()->GetAssetMap();
            for (auto element : assetMap)
            {
                // Get the upper case version of asset name
                sUpperAssetName = element.second->mName;
                for (uint32_t c = 0; c < sUpperAssetName.size(); ++c)
                {
                    sUpperAssetName[c] = toupper(sUpperAssetName[c]);
                }

                if (sUpperAssetName.find(filterStrUpper) != std::string::npos)
                {
                    filteredStubs.push_back(element.second);
                }
            }
        }
    }

    if (filterStr == "")
    {
        filteredStubs.clear();
    }

    if (currentDir != nullptr)
    {
        if (!showFilter || filterStr == "")
        {
            // Directories first
            ImGui::PushStyleColor(ImGuiCol_Header, kBgInactive);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kBgHover);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, kBgInactive);

            // Parent Dir (..)
            if (currentDir->mParentDir != nullptr)
            {
                if (ImGui::Selectable("..", true))
                {
                    GetEditorState()->SetAssetDirectory(currentDir->mParentDir, true);
                }
            }

            // Child Dirs
            for (uint32_t i = 0; i < currentDir->mChildDirs.size(); ++i)
            {
                AssetDir* childDir = currentDir->mChildDirs[i];

                if (ImGui::Selectable(childDir->mName.c_str(), true))
                {
                    GetEditorState()->SetAssetDirectory(childDir, true);
                }

                if (ImGui::BeginPopupContextItem())
                {
                    DrawAssetsContextPopup(nullptr, childDir);
                    ImGui::EndPopup();
                }
            }

            ImGui::PopStyleColor(3); // Pop Directory Colors
        }

        std::vector<AssetStub*>* stubs = &(currentDir->mAssetStubs);
        if (showFilter && filterStr != "")
        {
            stubs = &filteredStubs;
        }

        // Assets
        AssetStub* selStub = GetEditorState()->GetSelectedAssetStub();
        for (uint32_t i = 0; i < stubs->size(); ++i)
        {
            AssetStub* stub = (*stubs)[i];

            bool isSelectedStub = (stub == selStub);
            if (isSelectedStub)
            {
                ImGui::PushStyleColor(ImGuiCol_Header, kSelectedColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kSelectedColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSelectedColor);
            }

            glm::vec4 assetColor = AssetManager::Get()->GetEditorAssetColor(stub->mType);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(assetColor.r, assetColor.g, assetColor.b, assetColor.a));

            if (ImGui::Selectable(stub->mName.c_str(), isSelectedStub))
            {
                if (selStub != stub)
                {
                    GetEditorState()->SetSelectedAssetStub(stub);
                }
                else if (!IsControlDown())
                {
                    GetEditorState()->SetSelectedAssetStub(nullptr);
                }

                if (IsControlDown() &&
                    stub != nullptr &&
                    stub->mAsset != nullptr)
                {
                    GetEditorState()->InspectObject(stub->mAsset);
                }
            }

            if (GetEditorState()->mTrackSelectedAsset &&
                stub == GetEditorState()->mSelectedAssetStub)
            {
                ImGui::SetScrollHereY(0.5f);
                GetEditorState()->mTrackSelectedAsset = false;
            }

            ImGui::PopStyleColor(); // Pop asset color

            if (isSelectedStub)
            {
                ImGui::PopStyleColor(3);
            }

            if (interactive && ImGui::BeginPopupContextItem())
            {
                DrawAssetsContextPopup(stub, nullptr);
                ImGui::EndPopup();
            }
        }
    }

    // If no popup is open and we aren't inputting text...
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
        ImGui::IsWindowHovered() &&
        !ImGui::GetIO().WantTextInput &&
        interactive)
    {
        const bool ctrlDown = IsControlDown();
        const bool shiftDown = IsShiftDown();
        const bool altDown = IsAltDown();

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("Null Context");
        }

        if (filterStr != "")
        {
            if (IsMouseButtonJustDown(MOUSE_X1))
            {
                filterStr = "";
                filteredStubs.clear();
            }
        }
        else
        {
            if (IsMouseButtonJustDown(MOUSE_X1))
            {
                GetEditorState()->RegressDirPast();
            }
            else if (IsMouseButtonJustDown(MOUSE_X2))
            {
                GetEditorState()->ProgressDirFuture();
            }
        }

        if (currentDir != nullptr)
        {
            if (ctrlDown && IsKeyJustDown(KEY_N))
            {
                CreateNewAsset(Scene::GetStaticType(), "SC_Scene");
            }

            if (ctrlDown && IsKeyJustDown(KEY_M))
            {
                CreateNewAsset(Material::GetStaticType(), "M_Material");
            }

            if (ctrlDown && IsKeyJustDown(KEY_P))
            {
                CreateNewAsset(ParticleSystem::GetStaticType(), "P_Particle");
            }
        }

        if (ctrlDown && IsKeyJustDown(KEY_D))
        {
            AssetStub* srcStub = GetEditorState()->GetSelectedAssetStub();

            if (srcStub != nullptr)
            {
                GetEditorState()->DuplicateAsset(srcStub);
            }
        }

        if (IsKeyJustDown(KEY_DELETE))
        {
            AssetStub* selStub = GetEditorState()->GetSelectedAssetStub();

            if (selStub != nullptr)
            {
                ActionManager::Get()->DeleteAsset(selStub);
            }
        }
    }

    if (interactive && ImGui::BeginPopup("Null Context"))
    {
        DrawAssetsContextPopup(nullptr, nullptr);
        ImGui::EndPopup();
    }
}

static void DrawAssetsPanel()
{
    const float halfHeight = (float)GetEngineState()->mWindowHeight / 2.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, halfHeight));
    ImGui::SetNextWindowSize(ImVec2(kSidePaneWidth, halfHeight));

    ImGui::Begin("Assets", nullptr, kPaneWindowFlags);

    DrawAssetBrowser(true, true);

    ImGui::End();
}

static void DrawPropertiesPanel()
{
    const float dispWidth = (float)GetEngineState()->mWindowWidth;
    const float dispHeight = (float)GetEngineState()->mWindowHeight;

    ImGui::SetNextWindowPos(ImVec2(dispWidth - kSidePaneWidth, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(kSidePaneWidth, dispHeight));

    ImGui::Begin("Properties", nullptr, kPaneWindowFlags);

    if (ImGui::BeginTabBar("PropertyModeTabs"))
    {
        if (ImGui::BeginTabItem("Object"))
        {
            RTTI* obj = GetEditorState()->GetInspectedObject();

            if (obj != nullptr)
            {
                // Add lock button for inspected object.
                ImVec2 curPos = ImGui::GetCursorPos();
                //ImGui::SetCursorPos(ImVec2())
                ImVec2 lockPos = curPos;
                lockPos.y -= 0;
                lockPos.x = ImGui::GetWindowWidth() - 40;

                bool inspectLocked = GetEditorState()->IsInspectLocked();
                if (inspectLocked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                }

                ImGui::SetCursorPos(lockPos);
                if (ImGui::Button("L", ImVec2(20, 20)))
                {
                    // Toggle locked inspection
                    GetEditorState()->LockInspect(!inspectLocked);
                }

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
                    ImGui::SetTooltip("Lock");

                ImGui::SetCursorPos(curPos);

                if (inspectLocked)
                {
                    ImGui::PopStyleColor();
                }

                Texture* texObj = obj->As<Texture>();
                if (texObj != nullptr &&
                    texObj->GetResource()->mImage != nullptr)
                {
                    // Dealloc prev tex descriptor
                    if (sPrevInspectTexture != texObj)
                    {
                        DeviceWaitIdle();

                        if (sInspectTexId != 0)
                        {
                            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)sInspectTexId);
                            sInspectTexId = 0;
                        }

                        sInspectTexId = (VkDescriptorSet)ImGui_ImplVulkan_AddTexture(
                            texObj->GetResource()->mImage->GetSampler(),
                            texObj->GetResource()->mImage->GetView(),
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                        sPrevInspectTexture = texObj;
                    }

                    if (sInspectTexId != 0)
                    {
                        ImGui::Image(sInspectTexId, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), ImVec4(1,1,1,1), ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
                    }
                }

                std::vector<Property> props;
                obj->GatherProperties(props);

                DrawPropertyList(obj, props);
            }

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scene"))
        {
            EditScene* editScene = GetEditorState()->GetEditScene();
            Scene* scene = editScene ? editScene->mSceneAsset.Get<Scene>() : nullptr;

            if (scene != nullptr)
            {
                std::vector<Property> sceneProps;
                scene->GatherProperties(sceneProps);

                DrawPropertyList(scene, sceneProps);
            }

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Global"))
        {
            std::vector<Property> globalProps;
            GatherGlobalProperties(globalProps);

            DrawPropertyList(nullptr, globalProps);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
        ImGui::IsWindowHovered() &&
        !ImGui::GetIO().WantTextInput)
    {
        bool ctrlDown = IsControlDown();

        // Hotkey for toggling lock.
        if (ctrlDown && IsKeyJustDown(KEY_L))
        {
            GetEditorState()->LockInspect(!GetEditorState()->IsInspectLocked());
        }

        // Navigate inspection history.
        if (IsMouseButtonJustDown(MOUSE_X1))
        {
            GetEditorState()->RegressInspectPast();
        }
        else if (IsMouseButtonJustDown(MOUSE_X2))
        {
            GetEditorState()->ProgressInspectFuture();
        }
    }

    ImGui::End();
}

static void DrawViewportPanel()
{
    Renderer* renderer = Renderer::Get();
    ActionManager* am = ActionManager::Get();

    float viewportBarX = GetEditorState()->mShowLeftPane ? kSidePaneWidth : 0.0f;
    float viewportBarWidth = (float)GetEngineState()->mWindowWidth;
    if (GetEditorState()->mShowLeftPane)
        viewportBarWidth -= kSidePaneWidth;
    if (GetEditorState()->mShowRightPane)
        viewportBarWidth -= kSidePaneWidth;

    const ImGuiWindowFlags kViewportWindowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings; /* | ImGuiWindowFlags_AlwaysAutoResize*/ /*| ImGuiWindowFlags_NoBackground*/;

    ImGui::SetNextWindowPos(ImVec2(viewportBarX, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(viewportBarWidth, kViewportBarHeight));

    ImGui::Begin("Viewport", nullptr, kViewportWindowFlags);

    // (1) Draw File / View / World / Play buttons below
    if (ImGui::Button("File"))
        ImGui::OpenPopup("FilePopup");

    ImGui::SameLine();
    if (ImGui::Button("Edit"))
        ImGui::OpenPopup("EditPopup");

    ImGui::SameLine();
    if (ImGui::Button("View"))
        ImGui::OpenPopup("ViewPopup");

    ImGui::SameLine();
    if (ImGui::Button("World"))
        ImGui::OpenPopup("WorldPopup");

    ImGui::SameLine();
    bool inPie = IsPlayingInEditor();
    if (ImGui::Button(inPie ? "Stop" : "Play"))
    {
        if (inPie)
        {
            GetEditorState()->EndPlayInEditor();
        }
        else
        {
            GetEditorState()->BeginPlayInEditor();
        }
    }

    ImGui::SameLine();
    int curMode = (int)GetEditorState()->mMode;
    const char* modeStrings[] = { "Scene", "3D", "2D" };
    ImGui::SetNextItemWidth(70);
    ImGui::Combo("##EditorMode", &curMode, modeStrings, 3);
    GetEditorState()->SetEditorMode((EditorMode)curMode);

    bool openSaveSceneAsModal = false;

    if (ImGui::BeginPopup("FilePopup"))
    {
        EditScene* editScene = GetEditorState()->GetEditScene();

        if (ImGui::Selectable("Open Project"))
            am->OpenProject();
        if (ImGui::Selectable("New Project"))
            am->CreateNewProject();
        if (ImGui::Selectable("New Scene"))
            GetEditorState()->OpenEditScene(nullptr);
        if (editScene && ImGui::Selectable("Save Scene"))
        {
            Scene* scene = editScene->mSceneAsset.Get<Scene>();
            AssetStub* sceneStub = scene ? AssetManager::Get()->GetAssetStub(scene->GetName()) : nullptr;
            if (sceneStub != nullptr)
            {
                GetEditorState()->CaptureAndSaveScene(sceneStub, nullptr);
            }
            else
            {
                // Need to request name and create asset.
                openSaveSceneAsModal = true;
                strncpy(sPopupInputBuffer, "", kPopupInputBufferSize);
            }
        }
        if (editScene && ImGui::Selectable("Save Scene As..."))
        {
            // Need to request name and create asset.
            openSaveSceneAsModal = true;
            strncpy(sPopupInputBuffer, "", kPopupInputBufferSize);
        }
        if (ImGui::Selectable("Recapture All Scenes"))
            am->RecaptureAndSaveAllScenes();
        if (ImGui::Selectable("Resave All Assets"))
            am->ResaveAllAssets();
        if (ImGui::Selectable("Reload All Scripts"))
            ReloadAllScripts();
        //if (ImGui::Selectable("Import Scene"))
        //    YYY;
        if (ImGui::BeginMenu("Package Project"))
        {
            DrawPackageMenu();
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    if (GetEditorState()->mRequestSaveSceneAs)
    {
        openSaveSceneAsModal = true;
    }

    if (openSaveSceneAsModal)
    {
        ImGui::OpenPopup("Save Scene As");
    }

    // Center the "Save Scene As" modal
    if (ImGui::IsPopupOpen("Save Scene As"))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        GetEditorState()->mRequestSaveSceneAs = false;

        AssetDir* curDir = GetEditorState()->GetAssetDirectory();

        if (curDir == nullptr || curDir->mEngineDir || curDir == AssetManager::Get()->GetRootDirectory())
        {
            GetEditorState()->SetAssetDirectory(AssetManager::Get()->FindProjectDirectory(), true);
        }

        {
            ImGuiWindowFlags childFlags = ImGuiWindowFlags_None;
            ImGui::BeginChild("Dir Browser", ImVec2(250, 250), true, childFlags);
            DrawAssetBrowser(false, false);

            ImGui::EndChild();
        }


        if (curDir != nullptr && !curDir->mEngineDir)
        {
            AssetDir* dir = curDir;
            std::string dirString = dir->mName + "/";
            dir = dir->mParentDir;

            while (dir != nullptr)
            {
                if (dir->mParentDir == nullptr)
                    dirString = "/" + dirString;
                else
                    dirString = dir->mName + "/" + dirString;

                dir = dir->mParentDir;
            }

            ImGui::Text("Save scene to directory...");
            ImGui::Indent(10);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.7f, 1.0f));
            ImGui::Text(dirString.c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent(10);

            bool save = false;
            if (ImGui::InputText("Scene Name", sPopupInputBuffer, kPopupInputBufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                save = true;
            }

            if (ImGui::Button("Save"))
            {
                save = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            if (save)
            {
                std::string sceneName = sPopupInputBuffer;

                if (sceneName == "")
                {
                    sceneName = "SC_Scene";
                }

                AssetStub* stub = EditorAddUniqueAsset(sceneName.c_str(), curDir, Scene::GetStaticType(), true);
                OCT_ASSERT(stub != nullptr);

                GetEditorState()->CaptureAndSaveScene(stub, nullptr);

                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::Text("Invalid asset directory. Please navigate to a project directory.");
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("EditPopup"))
    {
        if (ImGui::Selectable("Undo"))
            am->Undo();
        if (ImGui::Selectable("Redo"))
            am->Redo();

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("ViewPopup"))
    {
        if (ImGui::Selectable("Wireframe"))
            renderer->SetDebugMode(renderer->GetDebugMode() == DEBUG_WIREFRAME ? DEBUG_NONE : DEBUG_WIREFRAME);
        if (ImGui::Selectable("Collision"))
            renderer->SetDebugMode(renderer->GetDebugMode() == DEBUG_COLLISION ? DEBUG_NONE : DEBUG_COLLISION);
        if (ImGui::Selectable("Proxy"))
            renderer->EnableProxyRendering(!renderer->IsProxyRenderingEnabled());
        if (ImGui::Selectable("Bounds"))
        {
            uint32_t newMode = (uint32_t(renderer->GetBoundsDebugMode()) + 1) % uint32_t(BoundsDebugMode::Count);
            renderer->SetBoundsDebugMode((BoundsDebugMode)newMode);
        }
        if (ImGui::Selectable("Grid"))
            ToggleGrid();
        if (ImGui::Selectable("Stats"))
            renderer->EnableStatsOverlay(!renderer->IsStatsOverlayEnabled());
        if (ImGui::Selectable("Preview Lighting"))
        {
            GetEditorState()->mPreviewLighting = !GetEditorState()->mPreviewLighting;
            LogDebug("Preview lighting %s", GetEditorState()->mPreviewLighting ? "enabled." : "disabled.");
        }

        if (GetEditorState()->GetEditorMode() == EditorMode::Scene2D)
        {
            if (ImGui::Selectable("Reset 2D Viewport"))
            {
                GetEditorState()->GetViewport2D()->ResetViewport();
            }
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("WorldPopup"))
    {
        if (ImGui::BeginMenu("Spawn Node"))
        {
            DrawAddNodeMenu(nullptr);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Spawn Basic 3D"))
        {
            DrawSpawnBasic3dMenu(nullptr, true);
            ImGui::EndMenu();
        }
        if (ImGui::Selectable("Clear World"))
            am->DeleteAllNodes();
        if (ImGui::Selectable("Bake Lighting"))
            renderer->BeginLightBake();
        if (ImGui::Selectable("Clear Baked Lighting"))
        {
            const std::vector<Node*>& nodes = GetWorld()->GatherNodes();
            for (uint32_t a = 0; a < nodes.size(); ++a)
            {
                StaticMesh3D* meshNode = nodes[a]->As<StaticMesh3D>();
                if (meshNode != nullptr)
                {
                    meshNode->ClearInstanceColors();
                }
            }
        }
        if (ImGui::Selectable("Toggle Transform Mode"))
            GetEditorState()->GetViewport3D()->ToggleTransformMode();

        ImGui::EndPopup();
    }

    // (2) Draw Scene tabs on top

    static int32_t sPrevActiveSceneIdx = 0;
    std::vector<EditScene>& scenes = GetEditorState()->mEditScenes;
    int32_t activeSceneIdx = GetEditorState()->mEditSceneIndex;
    bool sceneJustChanged = sPrevActiveSceneIdx != activeSceneIdx;

    const ImGuiTabBarFlags kSceneTabBarFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll;
    ImGui::SameLine(0.0f, 20.0f);

    if (scenes.size() > 0 &&
        ImGui::BeginTabBar("SceneTabBar", kSceneTabBarFlags))
    {
        int32_t openedTab = activeSceneIdx;

        for (int32_t n = 0; n < scenes.size(); n++)
        {
            // Push a unique ID in case we have scenes with duplicate names.
            ImGui::PushID(n);

            const EditScene& scene = scenes[n];

            bool opened = true;
            const char* sceneName = "[Unsaved]";

            if (scene.mSceneAsset != nullptr)
            {
                sceneName = scene.mSceneAsset.Get<Scene>()->GetName().c_str();
            }

            ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
            if (sceneJustChanged && n == activeSceneIdx)
            {
                tabFlags = ImGuiTabItemFlags_SetSelected;
            }

            if (ImGui::BeginTabItem(sceneName, &opened, tabFlags))
            {
                if (n != activeSceneIdx)
                {
                    openedTab = n;
                }

                ImGui::EndTabItem();
            }

            if (!opened)
            {
                GetEditorState()->CloseEditScene(n);
            }

            ImGui::PopID();
        }

        // Did we switch tabs? 
        if (!sceneJustChanged && 
            openedTab != activeSceneIdx)
        {
            GetEditorState()->OpenEditScene(openedTab);
        }

        ImGui::EndTabBar();
    }

    sPrevActiveSceneIdx = activeSceneIdx;


    // Draw 3D / 2D / Material combo box on top right corner.


    // Hotkey Menus
    if (GetEditorState()->GetViewport3D()->ShouldHandleInput())
    {
        const bool ctrlDown = IsControlDown();
        bool shiftDown = IsShiftDown();
        const bool altDown = IsAltDown();

        //ImGuiIO& io = ImGui::GetIO();
        //ImGui::SetNextWindowPos(io.MousePos);

        if (shiftDown && IsKeyJustDown(KEY_Q))
        {
            ImGui::OpenPopup("Spawn Basic 3D");
        }

        if (shiftDown && IsKeyJustDown(KEY_W))
        {
            ImGui::OpenPopup("Spawn Basic Widget");
        }

        if (shiftDown && IsKeyJustDown(KEY_A))
        {
            ImGui::OpenPopup("Spawn Node");
        }

        if (ctrlDown && IsKeyJustDown(KEY_N))
        {
            GetEditorState()->OpenEditScene(nullptr);
        }
    }

    if (ImGui::BeginPopup("Spawn Basic 3D"))
    {
        DrawSpawnBasic3dMenu(nullptr, true);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Spawn Basic Widget"))
    {
        DrawSpawnBasicWidgetMenu(GetEditorState()->GetSelectedWidget());
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Spawn Node"))
    {
        DrawAddNodeMenu(nullptr);
        ImGui::EndPopup();
    }

    ImGui::End();

}

static void Draw2dSelections()
{
    const std::vector<Node*>& selNodes = GetEditorState()->GetSelectedNodes();

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    ImColor mutliSelColor(0.7f, 1.0f, 0.0f, 1.0f);
    ImColor selColor(0.0f, 1.0f, 0.0f, 1.0f);
    ImColor hoverColor(0.0f, 1.0f, 1.0f, 1.0f);
    float thickness = 3.0f;
    glm::vec4 vp = Renderer::Get()->GetViewport();
    Rect boundsRect;
    boundsRect.mX = 0.0f;
    boundsRect.mY = 0.0f;
    boundsRect.mWidth = vp.z;
    boundsRect.mHeight = vp.w;

    // Draw multiselected widget rects.
    for (int32_t i = 0; i < int32_t(selNodes.size()) - 1; ++i)
    {
        Widget* widget = selNodes[i]->As<Widget>();

        if (widget)
        {
            Rect rect = widget->GetRect();

            if (rect.OverlapsRect(boundsRect))
            {
                rect.Clamp(boundsRect);

                float x = rect.mX + vp.x;
                float y = rect.mY + vp.y;
                float w = rect.mWidth;
                float h = rect.mHeight;
                draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), mutliSelColor, 0.0f, ImDrawFlags_None, thickness);
            }
        }
    }

    // Draw the most recent selection differently
    if (selNodes.size() > 0)
    {
        Widget* widget = selNodes.back()->As<Widget>();

        if (widget)
        {
            Rect rect = widget->GetRect();

            if (rect.OverlapsRect(boundsRect))
            {
                rect.Clamp(boundsRect);

                float x = rect.mX + vp.x;
                float y = rect.mY + vp.y;
                float w = rect.mWidth;
                float h = rect.mHeight;
                draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), selColor, 0.0f, ImDrawFlags_None, thickness);
            }
        }
    }

    // Draw the hovered rect
    Widget* hoveredWidget = GetEditorState()->GetViewport2D()->GetHoveredWidget();
    if (hoveredWidget != nullptr)
    {
        Rect rect = hoveredWidget->GetRect();

        if (rect.OverlapsRect(boundsRect))
        {
            rect.Clamp(boundsRect);

            float x = rect.mX + vp.x;
            float y = rect.mY + vp.y;
            float w = rect.mWidth;
            float h = rect.mHeight;
            draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), hoverColor, 0.0f, ImDrawFlags_None, thickness);
        }
    }
}

void EditorImguiInit()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Override theme
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.20f, 0.68f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.61f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.50f, 0.47f, 1.00f);



    // Set unactive window title bg equal to active title.
    colors[ImGuiCol_TitleBg] = colors[ImGuiCol_TitleBgActive];
}

void EditorImguiDraw()
{
    ImGui::NewFrame();

    if (EditorIsInterfaceVisible())
    {
        if (GetEditorState()->mShowLeftPane)
        {
            DrawScenePanel();
            DrawAssetsPanel();
        }

        if (GetEditorState()->mShowRightPane)
        {
            DrawPropertiesPanel();
        }

        DrawViewportPanel();

        if (GetEditorState()->GetEditorMode() == EditorMode::Scene2D)
        {
            Draw2dSelections();
        }

        DrawFileBrowser();
    }

    ImGui::Render();
}

void EditorImguiShutdown()
{
    ImGui::DestroyContext();
}

void EditorImguiPreShutdown()
{
    if (sInspectTexId != 0)
    {
        DeviceWaitIdle();
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)sInspectTexId);
        sInspectTexId = 0;
    }
}

void EditorImguiGetViewport(uint32_t& x, uint32_t& y, uint32_t& width, uint32_t& height)
{
    if (EditorIsInterfaceVisible())
    {
        x = 0;
        y = uint32_t(kViewportBarHeight + 0.5f);
        int32_t iWidth = (int32_t)GetEngineState()->mWindowWidth;
        int32_t iHeight = int32_t(GetEngineState()->mWindowHeight - kViewportBarHeight + 0.5f);

        if (GetEditorState()->mShowLeftPane)
        {
            x = int32_t(kSidePaneWidth + 0.5f);
            iWidth -= int32_t(kSidePaneWidth + 0.5f);
        }
        if (GetEditorState()->mShowRightPane)
        {
            iWidth -= int32_t(kSidePaneWidth + 0.5f);
        }

        iWidth = glm::clamp<int32_t>(iWidth, 100, GetEngineState()->mWindowWidth);
        iHeight = glm::clamp<int32_t>(iHeight, 100, GetEngineState()->mWindowHeight);

        width = uint32_t(iWidth);
        height = uint32_t(iHeight);
    }
    else
    {
        x = 0;
        y = 0;
        width = GetEngineState()->mWindowWidth;
        height = GetEngineState()->mWindowHeight;
    }
}

bool EditorIsInterfaceVisible()
{
    return GetEditorState()->mShowInterface && (!IsPlaying() || GetEditorState()->mEjected);
}

#endif

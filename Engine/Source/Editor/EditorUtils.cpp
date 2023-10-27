#if EDITOR

#include "EditorUtils.h"
#include "Engine.h"
#include "Log.h"
#include "World.h"
#include "Nodes/3D/Camera3d.h"
#include "PanelManager.h"
#include "Widgets/ViewportPanel.h"
#include "Widgets/WidgetHierarchyPanel.h"
#include "AssetManager.h"
#include "Assets/Scene.h"
#include "ActionManager.h"

#include "Input/Input.h"
#include "InputDevices.h"

void EditorCenterCursor()
{
    int32_t centerX = GetEngineState()->mWindowWidth / 2;
    int32_t centerY = GetEngineState()->mWindowHeight / 2;
    INP_SetCursorPos(centerX, centerY);
}

glm::vec3 EditorGetFocusPosition()
{
    Camera3D* camera = GetWorld()->GetActiveCamera();
    float focalDistance = PanelManager::Get()->GetViewportPanel()->GetFocalDistance();
    glm::vec3 focusPos = camera->GetAbsolutePosition() + focalDistance * camera->GetForwardVector();

    return focusPos;
}

AssetStub* EditorAddUniqueAsset(const char* baseName, AssetDir* dir, TypeId assetType, bool autoCreate)
{
    AssetStub* stub = nullptr;
    std::string assetName;
    for (int32_t i = 0; i < 99; ++i)
    {
        assetName = baseName;

        if (i > 0)
        {
            assetName += "_";
            assetName += std::to_string(i);
        }

        if (!AssetManager::Get()->DoesAssetExist(assetName))
        {
            if (autoCreate)
            {
                stub = AssetManager::Get()->CreateAndRegisterAsset(assetType, dir, assetName, false);
            }
            else
            {
                stub = AssetManager::Get()->RegisterAsset(assetName, assetType, dir, nullptr, false);

                if (stub != nullptr)
                {
                    Asset* newAsset = Asset::CreateInstance(assetType);
                    stub->mAsset = newAsset;
                }
            }
            break;
        }
    }

    return stub;
}

std::string EditorGetAssetNameFromPath(const std::string& path)
{
    const char* lastSlash = strrchr(path.c_str(), '/');
    std::string filename = lastSlash ? (lastSlash + 1) : path;
    int32_t dotIndex = int32_t(filename.find_last_of('.'));
    std::string assetName = filename.substr(0, dotIndex);

    return assetName;
}

#endif

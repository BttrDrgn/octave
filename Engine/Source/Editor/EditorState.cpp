#if EDITOR

#include "EditorState.h"
#include "EditorConstants.h"
#include "PanelManager.h"
#include "ActionManager.h"
#include "Nodes/Node.h"
#include "Asset.h"
#include "AssetManager.h"
#include "Nodes/Node.h"
#include "Nodes/3D/Node3d.h"
#include "Engine.h"
#include "Grid.h"
#include "World.h"
#include "TimerManager.h"
#include "AudioManager.h"
#include "Assets/Scene.h"
#include "EditorUtils.h"
#include "Widgets/ActionList.h"
#include "Widgets/TextEntry.h"
#include "Widgets/WidgetViewportPanel.h"
#include "Widgets/PropertiesPanel.h"
#include "Input/Input.h"

static EditorState sEditorState;

constexpr const char* kEditorSaveFile = "Editor.sav";
constexpr int32_t kEditorSaveVersion = 1;

EditorState* GetEditorState()
{
    return &sEditorState;
}


void EditorState::Init()
{
    mEditorCamera = Node::Construct<Camera3D>();
    // TODO-NODE: This is a little sketchy because this will call World::RegisterNode(), but that's probably fine.
    mEditorCamera->SetWorld(GetWorld());
}

void EditorState::Shutdown()
{
    mEditorCamera->SetWorld(nullptr);
    Node::Destruct(mEditorCamera);
    mEditorCamera = nullptr;
}

void EditorState::SetEditorMode(EditorMode mode)
{
    // Only allow scene editing in PIE for now.
    if (IsPlayingInEditor())
    {
        mode = EditorMode::Scene;
    }

    if (mMode != mode)
    {
        EditorMode prevMode = mMode;
        mMode = mode;

        // TODO-NODE: I don't think we need this anymore. Remove commented call after verifying.
        //SetSelectedNode(nullptr);

        PanelManager::Get()->OnEditorModeChanged();

        ActionManager::Get()->ResetUndoRedo();
    }
}

EditorMode EditorState::GetEditorMode()
{
    return mMode;
}

void EditorState::ReadEditorSave()
{
    if (SYS_DoesSaveExist(kEditorSaveFile))
    {
        // TODO: Save an ini file instead of a binary file so it can easily be
        // edited by a user, especially if something goes wrong.
        Stream stream;
        SYS_ReadSave(kEditorSaveFile, stream);

        int32_t version = stream.ReadInt32();

        if (version == kEditorSaveVersion)
        {
            stream.ReadString(mStartupSceneName);
        }
        else
        {
            SYS_DeleteSave(kEditorSaveFile);
        }
    }
}

void EditorState::WriteEditorSave()
{
    Stream stream;
    stream.WriteInt32(kEditorSaveVersion);
    stream.WriteString(mStartupSceneName);

    SYS_WriteSave(kEditorSaveFile, stream);
}

void EditorState::SetSelectedNode(Node* newNode)
{
    // Check if the component is actually exiled (only exists in the undo history).
    if (newNode != nullptr && newNode->GetWorld() == nullptr)
    {
        return;
    }

    if (mSelectedNodes.size() != 1 ||
        mSelectedNodes[0] != newNode)
    {
        mSelectedNodes.clear();

        if (newNode != nullptr)
        {
            mSelectedNodes.push_back(newNode);
        }

        if (!IsShuttingDown())
        {
            PanelManager::Get()->OnSelectedNodeChanged();
            ActionManager::Get()->OnSelectedNodeChanged();
        }
    }
}

void EditorState::AddSelectedNode(Node* node, bool addAllChildren)
{
    if (node != nullptr)
    {
        if (addAllChildren)
        {
            for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
            {
                AddSelectedNode(node->GetChild(i), true);
            }
        }

        std::vector<Node*>& nodes = mSelectedNodes;
        auto it = std::find(nodes.begin(), nodes.end(), node);

        if (it != nodes.end())
        {
            // Move the node to the back of the vector so that 
            // it is considered the primary selected node.
            nodes.erase(it);
        }

        nodes.push_back(node);
    }
}

void EditorState::RemoveSelectedNode(Node* node)
{
    if (node != nullptr)
    {
        std::vector<Node*>& nodes = mSelectedNodes;
        auto it = std::find(nodes.begin(), nodes.end(), node);

        if (it != nodes.end())
        {
            // Move the node to the back of the vector so that 
            // it is considered the primary selected node.
            nodes.erase(it);
        }
    }
}

void EditorState::SetSelectedAssetStub(AssetStub* newStub)
{
    if (mSelectedAssetStub != newStub)
    {
        mSelectedAssetStub = newStub;
        if (newStub != nullptr &&
            newStub->mAsset == nullptr)
        {
            AssetManager::Get()->LoadAsset(*newStub);
        }

        PanelManager::Get()->OnSelectedAssetChanged();
    }
}

void EditorState::SetControlMode(ControlMode newMode)
{
    // Don't do anything if new mode is same as current mode or there is no component selected.
    if (mControlMode == newMode)
    {
        return;
    }

    ControlMode prevMode = mControlMode;

    mControlMode = newMode;

    if (prevMode == ControlMode::Pilot ||
        prevMode == ControlMode::Translate ||
        prevMode == ControlMode::Rotate ||
        prevMode == ControlMode::Scale ||
        prevMode == ControlMode::Pan ||
        prevMode == ControlMode::Orbit)
    {
        INP_ShowCursor(true);
        INP_LockCursor(false);
    }

    if (newMode == ControlMode::Pilot ||
        newMode == ControlMode::Translate ||
        newMode == ControlMode::Rotate ||
        newMode == ControlMode::Scale ||
        newMode == ControlMode::Pan ||
        newMode == ControlMode::Orbit)
    {
        INP_ShowCursor(false);
        INP_LockCursor(true);

        // But because of the event loop processing, we might get a bogus mouse motion event even after
        // we have just forced the position. So set a flag to let the viewport panel know that we need to
        // recenter the mouse next frame.
        mMouseNeedsRecenter = true;
    }

    // Always reset transform lock when switching control modes.
    SetTransformLock(TransformLock::None);
}

void EditorState::BeginPlayInEditor()
{
    if (mPlayInEditor)
        return;

    SetSelectedNode(nullptr);
    SetSelectedAssetStub(nullptr);
    PanelManager::Get()->GetPropertiesPanel()->InspectAsset(nullptr);

    ActionManager::Get()->ResetUndoRedo();

    // Save the current scene we want to play (and later restore)
    mPieEditSceneIdx = mEditSceneIndex;
    ShelveEditScene();

    // TODO-NODE: This is overkill since the root node of the scene should have been removed in ShelveEditScene()
    //   Maybe we just want to assert that the root node is null.
    GetWorld()->Clear();
    OCT_ASSERT(GetWorld()->GetRootNode() == nullptr);

    ShowEditorUi(false);
    Renderer::Get()->EnableProxyRendering(false);

    mPlayInEditor = true;

    // Fake-Initialize the Game
    //OctPreInitialize();
    OctPostInitialize();

    EditScene* editScene = GetEditScene(mPieEditSceneIdx);
    if (editScene != nullptr &&
        editScene->mRootNode != nullptr)
    {
        Node* clonedRoot = editScene->mRootNode->Clone(true, false);
        GetWorld()->SetRootNode(clonedRoot);
    }
}

void EditorState::EndPlayInEditor()
{
    if (!mPlayInEditor)
        return;

    glm::mat4 cameraTransform(1);
    if (GetWorld()->GetActiveCamera())
    {
        cameraTransform = GetWorld()->GetActiveCamera()->GetTransform();
    }

    GetWorld()->DestroyRootNode();
    GetTimerManager()->ClearAllTimers();

    AudioManager::StopAllSounds();

    // Fake Shutdown
    OctPreShutdown();
    OctPostShutdown();

    SetSelectedNode(nullptr);
    SetSelectedAssetStub(nullptr);
    PanelManager::Get()->GetPropertiesPanel()->InspectAsset(nullptr);

    ActionManager::Get()->ResetUndoRedo();

    ShowEditorUi(true);
    Renderer::Get()->EnableProxyRendering(true);

    mPlayInEditor = false;
    mEjected = false;
    mPaused = false;

    // Restore the scene we were working on
    OpenEditScene(mPieEditSceneIdx);

    if (GetWorld()->GetActiveCamera())
    {
        GetWorld()->GetActiveCamera()->SetTransform(cameraTransform);
    }
}

void EditorState::EjectPlayInEditor()
{
    if (mPlayInEditor &&
        !mEjected)
    {
        SetSelectedNode(nullptr);
        mInjectedCamera = GetWorld()->GetActiveCamera();

        if (mEjectedCamera == nullptr)
        {
            Camera3D* ejectedCamera = GetWorld()->SpawnNode<Camera3D>();
            ejectedCamera->SetName("Ejected Camera");
            mEjectedCamera = ejectedCamera;

            // Set its transform to match the PIE camera
            if (GetWorld()->GetActiveCamera())
            {
                ejectedCamera->SetTransform(GetWorld()->GetActiveCamera()->GetTransform());
            }
        }

        GetWorld()->SetActiveCamera(mEjectedCamera.Get<Camera3D>());
        ShowEditorUi(true);
        mEjected = true;
    }
}

void EditorState::InjectPlayInEditor()
{
    if (mPlayInEditor &&
        mEjected)
    {
        SetSelectedNode(nullptr);

        if (mInjectedCamera != nullptr)
        {
            GetWorld()->SetActiveCamera(mInjectedCamera.Get<Camera3D>());
        }

        ShowEditorUi(false);
        mEjected = false;
    }
}

void EditorState::SetPlayInEditorPaused(bool paused)
{
    mPaused = paused;
}

bool EditorState::IsPlayInEditorPaused()
{
    return mPaused;
}

Camera3D* EditorState::GetEditorCamera()
{
    return mEditorCamera;
}

void EditorState::LoadStartupScene()
{
    if (mStartupSceneName != "")
    {
        Scene* scene = LoadAsset<Scene>(mStartupSceneName);

        if (scene != nullptr)
        {
            ActionManager::Get()->OpenScene(scene);
        }
    }
}

Node* EditorState::GetSelectedNode()
{
    return (mSelectedNodes.size() > 0) ?
        mSelectedNodes.back() :
        nullptr;
}

const std::vector<Node*>& EditorState::GetSelectedNodes()
{
    return mSelectedNodes;
}

bool EditorState::IsNodeSelected(Node* node)
{
    for (uint32_t i = 0; i < mSelectedNodes.size(); ++i)
    {
        if (mSelectedNodes[i] == node)
        {
            return true;
        }
    }
    return false;
}

void EditorState::DeselectNode(Node* node)
{
    bool erased = false;
    for (uint32_t i = 0; i < mSelectedNodes.size(); ++i)
    {
        if (mSelectedNodes[i] == node)
        {
            mSelectedNodes.erase(mSelectedNodes.begin() + i);
            erased = true;
            break;
        }
    }

    if (erased && !IsShuttingDown())
    {
        PanelManager::Get()->OnSelectedNodeChanged();
        ActionManager::Get()->OnSelectedNodeChanged();
    }
}

void EditorState::OpenEditScene(Scene* scene)
{
    int32_t editSceneIdx = -1;
    EditScene* editScene = nullptr;

    // Allow opening multiple null scenes.
    if (scene != nullptr)
    {
        for (uint32_t i = 0; i < mEditScenes.size(); ++i)
        {
            if (mEditScenes[i].mSceneAsset == scene)
            {
                editScene = &mEditScenes[i];
                editSceneIdx = (int32_t)i;
                break;
            }
        }
    }

    if (editScene == nullptr)
    {
        // The scene isn't open yet,
        mEditScenes.push_back(EditScene());
        EditScene& newEditScene = mEditScenes.back();
        newEditScene.mSceneAsset = scene;
        if (scene != nullptr)
        {
            newEditScene.mRootNode = scene->Instantiate();
        }

        newEditScene.mCameraTransform = glm::mat4(1);

        editScene = &newEditScene;
        editSceneIdx = int32_t(mEditScenes.size()) - 1;
    }

    OCT_ASSERT(editScene != nullptr);
    OCT_ASSERT(editSceneIdx != -1);

    OpenEditScene(editSceneIdx);
}

void EditorState::OpenEditScene(int32_t idx)
{
    // Lock scene open/close during PIE
    if (mPlayInEditor)
        return;

    // Shelve whatever we are working on.
    ShelveEditScene();
    OCT_ASSERT(GetWorld()->GetRootNode() == nullptr);

    if (idx >= 0 && idx < int32_t(mEditScenes.size()))
    {
        const EditScene& editScene = mEditScenes[idx];
        mEditSceneIndex = idx;
        GetWorld()->SetRootNode(editScene.mRootNode); // could be nullptr.
        GetEditorCamera()->SetTransform(editScene.mCameraTransform);

        ActionManager::Get()->ResetUndoRedo();
    }
}

void EditorState::CloseEditScene(int32_t idx)
{
    // Lock scene open/close during PIE
    if (mPlayInEditor)
        return;

    if (idx >= 0 && idx < int32_t(mEditScenes.size()))
    {
        if (idx == mEditSceneIndex)
        {
            // Is this the active EditScene? If so, shelve it first.
            ShelveEditScene();
        }

        // Destroy the root node
        Node::Destruct(mEditScenes[idx].mRootNode);

        // Remove this EditScene entry.
        mEditScenes.erase(mEditScenes.begin() + idx);

        // If that was the active edit scene, then load the next one it's place.
        if (mEditSceneIndex == -1 && 
            mEditScenes.size() > 0)
        {
            if (idx >= int32_t(mEditScenes.size()))
            {
                idx = int32_t(mEditScenes.size() - 1);
            }

            OpenEditScene(idx);
        }
    }
}

void EditorState::ShelveEditScene()
{
    if (mEditSceneIndex >= 0)
    {
        EditScene& editScene = mEditScenes[mEditSceneIndex];
        editScene.mRootNode = GetWorld()->GetRootNode();
        editScene.mCameraTransform = GetEditorCamera()->GetTransform();
        GetWorld()->SetRootNode(nullptr);

        mEditSceneIndex = -1;

        ActionManager::Get()->ResetUndoRedo();
    }
}

EditScene* EditorState::GetEditScene(int32_t idx)
{
    EditScene* ret = nullptr;

    if (idx == -1)
    {
        // -1 means the current edit scene index.
        idx = mEditSceneIndex;
    }

    if (idx >= 0 &&
        idx < int32_t(mEditScenes.size()))
    {
        ret = &mEditScenes[mEditSceneIndex];
    }

    return ret;
}

void EditorState::ShowEditorUi(bool show)
{
    mUiEnabled = show;
}

Asset* EditorState::GetSelectedAsset()
{
    return  mSelectedAssetStub ? mSelectedAssetStub->mAsset : nullptr;
}

AssetStub* EditorState::GetSelectedAssetStub()
{
    return mSelectedAssetStub;
}

ControlMode EditorState::GetControlMode()
{
    return mControlMode;
}

glm::vec3 EditorState::GetTransformLockVector(TransformLock lock)
{
    glm::vec3 ret = glm::vec3(1.0, 1.0, 1.0);

    switch (lock)
    {
    case TransformLock::AxisX: ret = glm::vec3(1.0f, 0.0f, 0.0f); break;
    case TransformLock::AxisY: ret = glm::vec3(0.0f, 1.0f, 0.0f); break;
    case TransformLock::AxisZ: ret = glm::vec3(0.0f, 0.0f, 1.0f); break;
    case TransformLock::PlaneYZ: ret = glm::vec3(0.0f, 1.0f, 1.0f); break;
    case TransformLock::PlaneXZ: ret = glm::vec3(1.0f, 0.0f, 1.0f); break;
    case TransformLock::PlaneXY: ret = glm::vec3(1.0f, 1.0f, 0.0f); break;
    default: ret = glm::vec3(1.0, 1.0, 1.0); break;
    }

    return ret;
}

void EditorState::SetTransformLock(TransformLock lock)
{
    static Line lineX = Line({ 0,0,0 }, { 10, 0, 0 }, { 1.0f, 0.4f, 0.4f, 1.0f }, -1.0f);
    static Line lineY = Line({ 0,0,0 }, { 0, 10, 0 }, { 0.4f, 1.0f ,0.4f, 1.0f }, -1.0f);
    static Line lineZ = Line({ 0,0,0 }, { 0, 0, 10 }, { 0.4f, 0.4f, 1.0f, 1.0f }, -1.0f);

    mTransformLock = lock;

    World* world = GetWorld();

    if (world != nullptr)
    {
        world->RemoveLine(lineX);
        world->RemoveLine(lineY);
        world->RemoveLine(lineZ);

        Node* node = GetSelectedNode();
        if (node != nullptr && node->IsNode3D())
        {
            glm::vec3 pos = static_cast<Node3D*>(node)->GetAbsolutePosition();
            lineX.mStart = pos - glm::vec3(10000, 0, 0);;
            lineY.mStart = pos - glm::vec3(0, 10000, 0);;
            lineZ.mStart = pos - glm::vec3(0, 0, 10000);;
            lineX.mEnd = pos + glm::vec3(10000,0,0);
            lineY.mEnd = pos + glm::vec3(0, 10000, 0);
            lineZ.mEnd = pos + glm::vec3(0, 0, 10000);

            switch (lock)
            {
            case TransformLock::AxisX:
                world->AddLine(lineX);
                break;
            case TransformLock::AxisY:
                world->AddLine(lineY);
                break;
            case TransformLock::AxisZ:
                world->AddLine(lineZ);
                break;
            case TransformLock::PlaneYZ:
                world->AddLine(lineY);
                world->AddLine(lineZ);
                break;
            case TransformLock::PlaneXZ:
                world->AddLine(lineX);
                world->AddLine(lineZ);
                break;
            case TransformLock::PlaneXY:
                world->AddLine(lineX);
                world->AddLine(lineY);
                break;
            default:
                break;
            }

        }
    }
}

#endif
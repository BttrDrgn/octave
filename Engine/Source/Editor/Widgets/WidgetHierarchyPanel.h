#pragma once

#include "Panel.h"
#include "Nodes/Widgets/ModalList.h"

class Button;
class TextField;
class HierarchyButton;

class WidgetHierarchyPanel : public Panel
{
public:

    WidgetHierarchyPanel();
    void OnSelectedWidgetChanged();
    void RefreshButtons();

    virtual void HandleInput() override;
    virtual void Update() override;

    static const float sPanelHeight;

protected:

    static void ActionHandler(Button* button);
    static void ShowAddBasicWidgetPrompt();
    static void ShowAddNativeWidgetPrompt();
    static void ShowAddMappedWidgetPrompt();
    static void MoveWidgetSlot(Widget* widget, int32_t delta);
    static void DeleteWidget(Widget* widget);
    static void AttachSelectedWidget(Widget* newParent);
    static void SetRootWidgetAction(Widget* newRoot);
    static void HandleRenameWidget(TextField* tf);

    static const int32_t sNumButtons = 15;
    HierarchyButton* mButtons[sNumButtons];

    uint32_t mCachedNumWidgets = 0;
    Widget* mCachedEditRoot = nullptr;

    int32_t mListOffset;
};
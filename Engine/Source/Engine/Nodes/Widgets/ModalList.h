#pragma once

#include "Nodes/Widgets/VerticalList.h"

class ModalList : public VerticalList
{
public:

    DECLARE_WIDGET(ModalList, VerticalList);

    ModalList();

    virtual void Update() override;
};
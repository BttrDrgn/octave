#include "Nodes/Widgets/PolyRect.h"

FORCE_LINK_DEF(PolyRect);
DEFINE_WIDGET(PolyRect, Poly);

PolyRect::PolyRect()
{

}

PolyRect::~PolyRect()
{

}

void PolyRect::Update()
{
    Poly::Update();

    if (IsDirty())
    {
        float width = mRect.mWidth;
        float height = mRect.mHeight;

        ClearVertices();
        AddVertex({ 0.0f, 0.0f }, mColor);
        AddVertex({ 0.0f, height }, mColor);
        AddVertex({ width, height }, mColor);
        AddVertex({ width, 0.0f }, mColor);
        AddVertex({ 0.0f, 0.0f }, mColor);
    }
}


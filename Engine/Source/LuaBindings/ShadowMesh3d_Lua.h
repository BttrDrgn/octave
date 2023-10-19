#pragma once

#include "EngineTypes.h"
#include "Log.h"
#include "Engine.h"

#include "Nodes/3D/ShadowMesh3d.h"

#include "LuaBindings/Node_Lua.h"
#include "LuaBindings/LuaUtils.h"

#if LUA_ENABLED

#define SHADOW_MESH_COMPONENT_LUA_NAME "ShadowMesh3D"
#define SHADOW_MESH_COMPONENT_LUA_FLAG "cfShadowMesh3D"
#define CHECK_SHADOW_MESH_COMPONENT(L, arg) static_cast<ShadowMesh3D*>(CheckComponentLuaType(L, arg, SHADOW_MESH_COMPONENT_LUA_NAME, SHADOW_MESH_COMPONENT_LUA_FLAG));

struct ShadowMesh3D_Lua
{
    static void Bind();
};

#endif
#pragma once

#include "EngineTypes.h"
#include "Log.h"
#include "Engine.h"

#include "Nodes/3D/Capsule3d.h"

#include "LuaBindings/Node_Lua.h"
#include "LuaBindings/LuaUtils.h"

#if LUA_ENABLED

#define CAPSULE_COMPONENT_LUA_NAME "Capsule3D"
#define CAPSULE_COMPONENT_LUA_FLAG "cfCapsule3D"
#define CHECK_CAPSULE_COMPONENT(L, arg) static_cast<Capsule3D*>(CheckComponentLuaType(L, arg, CAPSULE_COMPONENT_LUA_NAME, CAPSULE_COMPONENT_LUA_FLAG));

struct Capsule3D_Lua
{
    static int GetHeight(lua_State* L);
    static int SetHeight(lua_State* L);
    static int GetRadius(lua_State* L);
    static int SetRadius(lua_State* L);

    static void Bind();
};

#endif

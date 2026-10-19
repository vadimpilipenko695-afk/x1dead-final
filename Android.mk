LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := x1dead
LOCAL_SRC_FILES := main.cpp \
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_opengl3.cpp imgui/backends/imgui_impl_android.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/imgui $(LOCAL_PATH)/imgui/backends
LOCAL_CPPFLAGS   := -std=c++20 -O2 -fvisibility=hidden -fno-exceptions -fno-rtti
LOCAL_LDLIBS     := -llog -landroid -lEGL -lGLESv3 -ldl
include $(BUILD_SHARED_LIBRARY)

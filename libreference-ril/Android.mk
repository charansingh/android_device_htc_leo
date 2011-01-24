# Copyright 2006 The Android Open Source Project

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= \
    leoreference-ril.c \
    misc.c \

LOCAL_SHARED_LIBRARIES := \
    libcutils libutils libril 

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE

LOCAL_PRELINK_MODULE := false
LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

ifeq (foo,foo)
  #build shared library
  LOCAL_SHARED_LIBRARIES += \
      libcutils libutils libdl
  LOCAL_LDLIBS += -lpthread -ldl
  LOCAL_CFLAGS += -DRIL_SHLIB
  LOCAL_MODULE:= libleo-reference-ril
  include $(BUILD_SHARED_LIBRARY)
else
  #build executable
  LOCAL_SHARED_LIBRARIES += \
       libril libdl
  LOCAL_MODULE:= leo-reference-ril
  include $(BUILD_EXECUTABLE)
endif

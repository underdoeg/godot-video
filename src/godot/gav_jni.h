#pragma once

#ifdef BUILD_ANDROID

#include <jni.h>

extern JNIEnv *gav_get_jnienv();
extern JavaVM *gav_get_jnivm();
#endif

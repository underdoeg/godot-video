

#include "gav_jni.h"


// define static JavaVM
static JavaVM *jvm = nullptr;
static JNIEnv *env = nullptr;

// this will be called when Android plugin initialize
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK)
		return JNI_ERR;
	jvm = vm;
	return JNI_VERSION_1_6;
}

JNIEnv *gav_get_jnienv() {
	return env;
}

JavaVM *gav_get_jnivm() {
	return jvm;
}

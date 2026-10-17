//
// Created by maks on 10.04.2026.
//

#include <android/api-level.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <jni.h>

#include <driver_helper/nsbypass.h>
#include <android/dlext.h>
#include <mojoexec.h>

// Name of the custom Vulkan driver to load, or NULL to use the system driver
static const char* custom_vulkan_driver = NULL;

// Path to the PanVK driver .so file (set from Java). If NULL, falls back to libvulkan_panfrost.so
static char* panvk_driver_path = NULL;

#ifdef ENABLE_CUSTOM_VULKAN_LOADER
bool load_custom_vulkan() {
    static bool driver_loaded = false;
    if(driver_loaded) return true;
    if(custom_vulkan_driver == NULL) return false;

    const char* cache_dir = getenv("TMPDIR");
    if(!linker_ns_load(mojoexec_native_dir)) return NULL;
    void* linkerhook = linker_ns_dlopen("liblinkerhook.so", RTLD_LOCAL | RTLD_NOW);
    if(linkerhook == NULL) return NULL;
    void* driver_handle = linker_ns_dlopen(custom_vulkan_driver, RTLD_LOCAL | RTLD_NOW);
    if(driver_handle == NULL) {
        printf("MojoExec: Failed to load %s!\n%s\n", custom_vulkan_driver, dlerror());
        goto fail_l;
    }

    void* dl_android = linker_ns_dlopen("libdl_android.so", RTLD_LOCAL | RTLD_LAZY);
    if(dl_android == NULL) goto fail_t;

    void* android_get_exported_namespace = dlsym(dl_android, "android_get_exported_namespace");
    void (*linkerhook_pass_handles)(void*, void*, void*) = dlsym(linkerhook, "app__pojav_linkerhook_pass_handles");

    if(linkerhook_pass_handles == NULL || android_get_exported_namespace == NULL) goto fail_d;
    linkerhook_pass_handles(driver_handle, android_dlopen_ext, android_get_exported_namespace);

    void* libvulkan = linker_ns_dlopen_unique(cache_dir, "libvulkan.so", "libmjlvlk.so", RTLD_LOCAL | RTLD_NOW);
    printf("MojoExec: Loaded mjlvlk, ptr=%p\n", libvulkan);
    if(libvulkan) {
        driver_loaded = true;
        return true;
    }
    fail_d: dlclose(dl_android);
    fail_t: dlclose(driver_handle);
    fail_l: dlclose(linkerhook);
    return false;
}
#endif

void* mojoexec_acq_vulkan_handle() {
    int flags = RTLD_LOCAL | RTLD_NOW;
#ifdef ENABLE_CUSTOM_VULKAN_LOADER
    if(android_get_device_api_level() >= 28) { // the loader does not support below that
        if(custom_vulkan_driver != NULL && load_custom_vulkan())
            // Reference the vulkan driver separately to avoid weirdness from libraries calling dlclose
            return linker_ns_dlopen("libmjlvlk.so", flags);
    }
#endif
    void* vulkan_ptr = dlopen("libvulkan.so", flags);
    printf("MojoExec: loaded system vulkan, ptr=%p\n", vulkan_ptr);
    return vulkan_ptr;
}

JNIEXPORT void JNICALL
Java_git_artdeell_mojoexec_MojoExec_setUseTurnip(JNIEnv *env, jclass clazz, jboolean enable) {
    custom_vulkan_driver = enable ? "libvulkan_freedreno.so" : NULL;
}

JNIEXPORT void JNICALL
Java_git_artdeell_mojoexec_MojoExec_setUsePanvk(JNIEnv *env, jclass clazz, jboolean enable) {
    custom_vulkan_driver = enable ? (panvk_driver_path != NULL ? panvk_driver_path : "libvulkan_panfrost.so") : NULL;
}

JNIEXPORT void JNICALL
Java_git_artdeell_mojoexec_MojoExec_setPanvkDriverPath(JNIEnv *env, jclass clazz, jstring path) {
    if(panvk_driver_path != NULL) free(panvk_driver_path);
    if(path == NULL) {
        panvk_driver_path = NULL;
        return;
    }
    const char* str = (*env)->GetStringUTFChars(env, path, NULL);
    panvk_driver_path = strdup(str);
    (*env)->ReleaseStringUTFChars(env, path, str);
}

// Does nothing if custom drivers are unsupported - Mesa will load the system driver automatically
JNIEXPORT void JNICALL
Java_git_artdeell_mojoexec_MojoExec_preloadVulkan(JNIEnv *env, jclass clazz) {
#ifdef ENABLE_CUSTOM_VULKAN_LOADER
    if(custom_vulkan_driver == NULL) return;
    if(!load_custom_vulkan()) {
        printf("MojoExec: Failed to preload %s!\n", custom_vulkan_driver);
    }
#endif
}

#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "zygisk.h"

static constexpr jchar kPhotosPkgUtf16[] = {
    'c','o','m','.','g','o','o','g','l','e','.','a','n','d','r','o','i','d','.','a','p','p','s','.','p','h','o','t','o','s'
};
static constexpr jsize kPhotosPkgLen = sizeof(kPhotosPkgUtf16) / sizeof(jchar);
static constexpr size_t kDexBufCapacity = 4096;

typedef struct {
    char name[16];
    char value[72];
} BuildField;

static constexpr BuildField kBuildFields[] = {
    {"MANUFACTURER", "Google"},
    {"MODEL",        "Pixel"},
    {"FINGERPRINT",  "google/sailfish/sailfish:9/PPR1.180610.009/4898911:user/release-keys"},
    {"BRAND",        "google"},
    {"PRODUCT",      "sailfish"},
    {"DEVICE",       "sailfish"},
    {"HARDWARE",     "marlin"},
    {"BOARD",        "sailfish"},
    {"ID",           "PPR1.180610.009"},
    {"DISPLAY",      "PPR1.180610.009"},
    {"TYPE",         "user"},
    {"TAGS",         "release-keys"},
};

static constexpr BuildField kVersionFields[] = {
    {"RELEASE",        "9"},
    {"INCREMENTAL",    "4898911"},
    {"SECURITY_PATCH", "2018-08-05"},
};

static struct ZygiskApiTable *g_tbl = nullptr;
static JNIEnv *g_env = nullptr;
static int g_dex_fd = -1;

[[gnu::always_inline]] static inline void set_option(enum ZygiskOption opt) {
    if (g_tbl && g_tbl->setOption) g_tbl->setOption(g_tbl->impl, opt);
}

[[gnu::always_inline]] static inline bool is_photos_proc(JNIEnv *restrict env, jstring nice_name) {
    if (__builtin_expect(!nice_name, 0)) return false;
    const auto len = (*env)->GetStringLength(env, nice_name);
    if (__builtin_expect(len < kPhotosPkgLen, 1)) return false;

    if (__builtin_expect(len > kPhotosPkgLen, 0)) {
        jchar sep = 0;
        (*env)->GetStringRegion(env, nice_name, kPhotosPkgLen, 1, &sep);
        if (sep != ':') return false;
    }

    jchar buf[kPhotosPkgLen] = {};
    (*env)->GetStringRegion(env, nice_name, 0, kPhotosPkgLen, buf);
    return __builtin_memcmp(buf, kPhotosPkgUtf16, sizeof(buf)) == 0;
}

static void set_static_strings(JNIEnv *restrict env, jclass cls, const BuildField *restrict fields, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto fid = (*env)->GetStaticFieldID(env, cls, fields[i].name, "Ljava/lang/String;");
        if (__builtin_expect(!fid, 0)) {
            (*env)->ExceptionClear(env);
            continue;
        }
        const auto val = (*env)->NewStringUTF(env, fields[i].value);
        if (__builtin_expect(val != nullptr, 1))
            (*env)->SetStaticObjectField(env, cls, fid, val);
    }
}

[[gnu::cold]] static void spoof_build_and_pm(JNIEnv *restrict env, const uint8_t *restrict dex_data, uint32_t dex_size) {
    if ((*env)->PushLocalFrame(env, 32) < 0) return;

    const auto build_cls = (*env)->FindClass(env, "android/os/Build");
    if (__builtin_expect(build_cls != nullptr, 1))
        set_static_strings(env, build_cls, kBuildFields, sizeof(kBuildFields) / sizeof(kBuildFields[0]));

    const auto ver_cls = (*env)->FindClass(env, "android/os/Build$VERSION");
    if (__builtin_expect(ver_cls != nullptr, 1)) {
        set_static_strings(env, ver_cls, kVersionFields, sizeof(kVersionFields) / sizeof(kVersionFields[0]));
        const auto sdk_fid = (*env)->GetStaticFieldID(env, ver_cls, "DEVICE_INITIAL_SDK_INT", "I");
        if (__builtin_expect(sdk_fid != nullptr, 1))
            (*env)->SetStaticIntField(env, ver_cls, sdk_fid, 25);
        else
            (*env)->ExceptionClear(env);
    }

    const auto apm_cls = (*env)->FindClass(env, "android/app/ApplicationPackageManager");
    if (__builtin_expect(apm_cls != nullptr, 1)) {
        const auto dis_mid = (*env)->GetStaticMethodID(env, apm_cls, "disableHasSystemFeatureCache", "()V");
        if (__builtin_expect(dis_mid != nullptr, 1))
            (*env)->CallStaticVoidMethod(env, apm_cls, dis_mid);
        (*env)->ExceptionClear(env);
    }

    const auto at_cls = (*env)->FindClass(env, "android/app/ActivityThread");
    if (__builtin_expect(at_cls != nullptr, 1)) {
        const auto get_pm_mid = (*env)->GetStaticMethodID(
            env, at_cls, "getPackageManager", "()Landroid/content/pm/IPackageManager;");
        const auto s_pm_fid = (*env)->GetStaticFieldID(
            env, at_cls, "sPackageManager", "Landroid/content/pm/IPackageManager;");
        const auto real_pm = (get_pm_mid && s_pm_fid)
            ? (*env)->CallStaticObjectMethod(env, at_cls, get_pm_mid) : nullptr;

        if (__builtin_expect(real_pm != nullptr, 1)) {
            const auto dex_arr = (*env)->NewByteArray(env, (jsize)dex_size);
            const auto bb_cls = (*env)->FindClass(env, "java/nio/ByteBuffer");
            const auto wrap_mid = (dex_arr && bb_cls)
                ? (*env)->GetStaticMethodID(env, bb_cls, "wrap", "([B)Ljava/nio/ByteBuffer;") : nullptr;

            if (__builtin_expect(wrap_mid != nullptr, 1)) {
                (*env)->SetByteArrayRegion(env, dex_arr, 0, (jsize)dex_size, (const jbyte *)dex_data);
                const auto byte_buf = (*env)->CallStaticObjectMethod(env, bb_cls, wrap_mid, dex_arr);
                const auto dex_cl_cls = (*env)->FindClass(env, "dalvik/system/InMemoryDexClassLoader");
                const auto dex_cl_init = (byte_buf && dex_cl_cls)
                    ? (*env)->GetMethodID(env, dex_cl_cls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V")
                    : nullptr;
                const auto dex_cl = dex_cl_init
                    ? (*env)->NewObject(env, dex_cl_cls, dex_cl_init, byte_buf, nullptr) : nullptr;

                if (__builtin_expect(dex_cl != nullptr, 1)) {
                    const auto load_mid = (*env)->GetMethodID(
                        env, dex_cl_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                    const auto entry_name = (*env)->NewStringUTF(env, "com.rev4n.unlimitedphotos.EntryPoint");
                    const auto entry_cls = (load_mid && entry_name)
                        ? (jclass)(*env)->CallObjectMethod(env, dex_cl, load_mid, entry_name) : nullptr;
                    const auto create_mid = entry_cls
                        ? (*env)->GetStaticMethodID(env, entry_cls, "createProxy", "(Ljava/lang/Object;)Ljava/lang/Object;")
                        : nullptr;
                    const auto proxy_pm = create_mid
                        ? (*env)->CallStaticObjectMethod(env, entry_cls, create_mid, real_pm) : nullptr;

                    if (__builtin_expect(proxy_pm != nullptr, 1))
                        (*env)->SetStaticObjectField(env, at_cls, s_pm_fid, proxy_pm);
                }
            }
        }
    }

    (*env)->ExceptionClear(env);
    (*env)->PopLocalFrame(env, nullptr);
}

[[gnu::hot]] static void pre_app_specialize(void *impl [[maybe_unused]], struct ZygiskAppSpecializeArgs *args) {
    if (__builtin_expect(!args || !args->nice_name || !is_photos_proc(g_env, *args->nice_name), 1)) {
        set_option(DLCLOSE_MODULE_LIBRARY);
        return;
    }

    set_option(FORCE_DENYLIST_UNMOUNT);
    const auto dirfd = (g_tbl && g_tbl->getModuleDir) ? g_tbl->getModuleDir(g_tbl->impl) : -1;
    if (dirfd < 0) return;

    g_dex_fd = openat(dirfd, "classes.dex", O_RDONLY | O_CLOEXEC);
    close(dirfd);
}

[[gnu::cold]] static void post_app_specialize(void *impl [[maybe_unused]], const struct ZygiskAppSpecializeArgs *args [[maybe_unused]]) {
    if (g_dex_fd >= 0) {
        alignas(16) uint8_t dex_buf[kDexBufCapacity] = {};
        const auto n = read(g_dex_fd, dex_buf, sizeof(dex_buf));
        close(g_dex_fd);
        g_dex_fd = -1;
        if (n > 0 && (size_t)n < sizeof(dex_buf))
            spoof_build_and_pm(g_env, dex_buf, (uint32_t)n);
    }
    set_option(DLCLOSE_MODULE_LIBRARY);
}

[[gnu::cold]] static void pre_server_specialize(void *impl [[maybe_unused]], struct ZygiskServerSpecializeArgs *args [[maybe_unused]]) {
    set_option(DLCLOSE_MODULE_LIBRARY);
}

[[gnu::cold]] static void post_server_specialize(void *impl [[maybe_unused]], const struct ZygiskServerSpecializeArgs *args [[maybe_unused]]) {}

static struct ZygiskModuleAbi g_abi = {
    ZYGISK_API_VERSION,
    &g_abi,
    pre_app_specialize,
    post_app_specialize,
    pre_server_specialize,
    post_server_specialize,
};

[[gnu::visibility("default"), gnu::used]]
void zygisk_module_entry(struct ZygiskApiTable *table, JNIEnv *env) {
    if (!table || !table->registerModule || !table->registerModule(table, &g_abi)) return;
    g_tbl = table;
    g_env = env;
}

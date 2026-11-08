#pragma once

#include <jni.h>
#include <stdint.h>
#include <sys/types.h>

#define ZYGISK_API_VERSION 5

enum ZygiskOption {
    FORCE_DENYLIST_UNMOUNT = 0,
    DLCLOSE_MODULE_LIBRARY = 1,
};

struct ZygiskAppSpecializeArgs {
    jint *uid;
    jint *gid;
    jintArray *gids;
    jint *runtime_flags;
    jobjectArray *rlimits;
    jint *mount_external;
    jstring *se_info;
    jstring *nice_name;
    jstring *instruction_set;
    jstring *app_data_dir;
    jintArray *fds_to_ignore;
    jboolean *is_child_zygote;
    jboolean *is_top_app;
    jobjectArray *pkg_data_info_list;
    jobjectArray *whitelisted_data_info_list;
    jboolean *mount_data_dirs;
    jboolean *mount_storage_dirs;
    jboolean *mount_sysprop_overrides;
};

struct ZygiskServerSpecializeArgs;
struct ZygiskApiTable;

struct ZygiskModuleAbi {
    long api_version;
    void *impl;
    void (*preAppSpecialize)(void *, struct ZygiskAppSpecializeArgs *);
    void (*postAppSpecialize)(void *, const struct ZygiskAppSpecializeArgs *);
    void (*preServerSpecialize)(void *, struct ZygiskServerSpecializeArgs *);
    void (*postServerSpecialize)(void *, const struct ZygiskServerSpecializeArgs *);
};

struct ZygiskApiTable {
    void *impl;
    bool (*registerModule)(struct ZygiskApiTable *, struct ZygiskModuleAbi *);
    void (*hookJniNativeMethods)(JNIEnv *, const char *, JNINativeMethod *, int);
    void (*pltHookRegister)(dev_t, ino_t, const char *, void *, void **);
    bool (*exemptFd)(int);
    bool (*pltHookCommit)(void);
    int  (*connectCompanion)(void *);
    void (*setOption)(void *, enum ZygiskOption);
    int  (*getModuleDir)(void *);
    uint32_t (*getFlags)(void *);
};

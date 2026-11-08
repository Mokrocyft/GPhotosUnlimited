# GPhotosUnlimited

A lightweight **Zygisk (API v5 / v4)** module that enables unlimited original-quality photo and video backup in **Google Photos** (`com.google.android.apps.photos`) by spoofing Pixel (2016 `sailfish`) build fields and `IPackageManager.hasSystemFeature` in-memory.

## Requirements
- Root manager (Magisk / KernelSU / APatch)
- **Zygisk** (Zygisk Next / NyaZygisk / ReZygisk / built-in Zygisk)
- Android 15+ (API 35+), `arm64-v8a`

## Architecture
- **Pure C26/C2y native core (`src/hook.c`)**:
  - Zero-heap UTF-16 package check (`GetStringRegion`) immediately unloads (`DLCLOSE_MODULE_LIBRARY`) from all non-Photos processes in ~2 ns.
  - Spoofs `android.os.Build` and `android.os.Build$VERSION` fields only inside `com.google.android.apps.photos`, disables `ApplicationPackageManager.disableHasSystemFeatureCache()`, and loads an in-memory `IPackageManager` dynamic proxy via `InMemoryDexClassLoader` backed by an ART-managed `byte[]`.
- **Zero-allocation Java proxy (`src/EntryPoint.java`)**:
  - Intercepts `hasSystemFeature` using a single compiled `switch` over Pixel 2016 preload feature flags without static collections.
- **16 KB page-size ready**: Linked with `-z max-page-size=16384` and `--pack-dyn-relocs=relr`.

## Build
Requires **Gradle 9.9.0** (via `./gradlew`), **CMake**, **Android SDK Build-Tools** (`d8`), and **Android NDK**:

```sh
./gradlew zipRelease
```

The flashable module archive is written to `release/GPhotosUnlimited-v7.1-release.zip`.

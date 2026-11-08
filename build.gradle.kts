import org.gradle.api.DefaultTask
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.api.tasks.bundling.Zip
import org.gradle.process.ExecOperations
import java.io.File
import javax.inject.Inject

@CacheableTask
abstract class CompileNativeTask : DefaultTask() {
    @get:Inject abstract val execOps: ExecOperations
    @get:InputFile @get:PathSensitive(PathSensitivity.NONE) abstract val sourceFile: RegularFileProperty
    @get:InputFile @get:PathSensitive(PathSensitivity.NONE) abstract val headerFile: RegularFileProperty
    @get:Input abstract val ndkDir: Property<String>
    @get:OutputFile abstract val outputFile: RegularFileProperty

    @TaskAction
    fun compile() {
        val ndk = File(ndkDir.get())
        val os = System.getProperty("os.name").lowercase()
        val tag = if ("win" in os) "windows-x86_64" else if ("mac" in os) "darwin-x86_64" else "linux-x86_64"
        val prebuilt = ndk.resolve("toolchains/llvm/prebuilt/$tag")
        val sysroot = prebuilt.resolve("sysroot")
        val rev = ndk.resolve("source.properties").takeIf { it.isFile }?.readLines()
            ?.firstOrNull { it.startsWith("Pkg.Revision") }?.substringAfter('=')?.trim() ?: ndk.name
        val ndkMajor = rev.substringBefore('.').toIntOrNull() ?: 0
        val api = if (ndkMajor >= 30) 37 else 35
        val out = outputFile.get().asFile.apply { parentFile.mkdirs() }
        val clang23 = System.getenv("PATH")?.split(File.pathSeparator)
            ?.map { File(it, "clang-23") }?.firstOrNull { it.canExecute() }
        val compiler = clang23?.absolutePath
            ?: prebuilt.resolve("bin/aarch64-linux-android$api-clang${if ("win" in os) ".cmd" else ""}").absolutePath
        val rtDir = prebuilt.resolve("lib/clang").listFiles()?.firstOrNull { it.isDirectory }?.resolve("lib/linux")

        val args = mutableListOf(
            compiler,
            "--target=aarch64-linux-android$api",
            "--sysroot=${sysroot.absolutePath}",
            "-shared",
            sourceFile.get().asFile.absolutePath,
            "-o", out.absolutePath,
            "-std=c2y", "-O3", "-flto", "-fuse-ld=lld",
            "-march=armv9.2-a+crypto",
            "-mbranch-protection=standard",
            "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fno-stack-protector",
            "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections",
            "-fmerge-all-constants", "-fomit-frame-pointer", "-fno-plt", "-fno-ident",
            "-Wall", "-Wextra", "-Werror",
            "-nodefaultlibs"
        )
        if (ndkMajor >= 28 || clang23 != null) args += "-mtune=oryon-1"
        if (rtDir != null) args += listOf("-L${rtDir.absolutePath}", "-l:libclang_rt.builtins-aarch64-android.a")
        args += listOf(
            "-lc",
            "-Wl,-O2,--gc-sections,--icf=all,--strip-all,--pack-dyn-relocs=relr," +
                "--hash-style=gnu,--build-id=none,-Bsymbolic,--exclude-libs,ALL,--no-undefined," +
                "-z,max-page-size=16384,-z,common-page-size=16384,-z,relro,-z,now,-z,noexecstack"
        )
        execOps.exec { commandLine(args) }
    }
}

@CacheableTask
abstract class CompileDexTask : DefaultTask() {
    @get:Inject abstract val execOps: ExecOperations
    @get:InputFile @get:PathSensitive(PathSensitivity.NONE) abstract val javaSource: RegularFileProperty
    @get:Input abstract val sdkDir: Property<String>
    @get:OutputFile abstract val outputDex: RegularFileProperty

    @TaskAction
    fun buildDex() {
        val outDex = outputDex.get().asFile
        val classesDir = outDex.parentFile.resolve("classes").apply {
            deleteRecursively()
            mkdirs()
        }
        val javaHome = System.getProperty("java.home")
        val isWin = "win" in System.getProperty("os.name").lowercase()
        val javac = File(javaHome, "bin/javac${if (isWin) ".exe" else ""}").absolutePath

        execOps.exec {
            commandLine(
                javac, "--release", "21", "-g:none",
                "-d", classesDir.absolutePath, javaSource.get().asFile.absolutePath
            )
        }

        val btRoot = File(sdkDir.get(), "build-tools")
        val d8 = btRoot.listFiles()?.filter { it.isDirectory }
            ?.sortedByDescending { it.name }
            ?.map { File(it, if (isWin) "d8.bat" else "d8") }
            ?.firstOrNull { it.isFile }
            ?: error("d8 not found under $btRoot")

        val classFiles = classesDir.walk().filter { it.isFile && it.extension == "class" }.map { it.absolutePath }.toList()
        val cmd = mutableListOf(d8.absolutePath, "--release", "--min-api", "35", "--output", outDex.parentFile.absolutePath)
        cmd.addAll(classFiles)
        execOps.exec { commandLine(cmd) }
    }
}

val outSo = layout.buildDirectory.file("native/arm64-v8a.so")
val outDex = layout.buildDirectory.file("dex/classes.dex")

val sdk = providers.environmentVariable("ANDROID_HOME")
    .orElse(providers.environmentVariable("ANDROID_SDK_ROOT"))
    .orElse("/usr/local/lib/android/sdk")

val ndk = providers.environmentVariable("ANDROID_NDK_HOME")
    .orElse(providers.environmentVariable("ANDROID_NDK_ROOT"))
    .orElse(
        sdk.map { s ->
            File(s, "ndk").listFiles()?.filter { it.isDirectory }
                ?.maxByOrNull { it.name.substringBefore('.').toIntOrNull() ?: 0 }?.absolutePath
                ?: error("No NDK found under $s/ndk")
        }
    )

val buildNative = tasks.register<CompileNativeTask>("buildNative") {
    sourceFile.set(layout.projectDirectory.file("src/hook.c"))
    headerFile.set(layout.projectDirectory.file("src/zygisk.h"))
    ndkDir.set(ndk)
    outputFile.set(outSo)
}

val buildDex = tasks.register<CompileDexTask>("buildDex") {
    javaSource.set(layout.projectDirectory.file("src/EntryPoint.java"))
    sdkDir.set(sdk)
    outputDex.set(outDex)
}

tasks.register<Zip>("zipRelease") {
    dependsOn(buildNative, buildDex)
    archiveFileName.set("GPhotosUnlimited-v7.1-release.zip")
    destinationDirectory.set(layout.projectDirectory.dir("release"))
    from(layout.projectDirectory.dir("template"))
    from(outDex)
    from(outSo) { into("zygisk") }
}

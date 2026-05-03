#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <dirent.h>

/**
 * @brief Ensure a directory exists, creating it if necessary.
 *
 * @param path Directory path to check/create.
 * @param mode Permissions to use if creating the directory.
 * @return 1 if the directory exists or was created, 0 on failure.
 */
static int clrhook_ensure_directory(const char *path, mode_t mode)
{
    struct stat st = {0};
    if (stat(path, &st) == -1)
    {
        if (mkdir(path, mode) != 0)
        {
            fprintf(stderr, "[clrhook] Failed to create directory: %s\n", path);
            return 0;
        }
        // LOGV("[clrhook] Created directory: %s\n", path);
    }
    return 1;
}

// MonoClass* mono_class_from_name(MonoImage *image, const char* namespace, const char* name)
typedef void *(*mono_class_from_name_t)(void *, const char *, const char *);
static mono_class_from_name_t real_mono_class_from_name = NULL;

// MonoMethod* mono_class_get_method_from_name(MonoClass *klass, const char* name, int param_count)
typedef void *(*mono_class_get_method_from_name_t)(void *, const char *, int);
static mono_class_get_method_from_name_t real_mono_class_get_method_from_name = NULL;

// MonoAssembly* mono_domain_assembly_open(MonoDomain *domain, const char *name)
typedef void *(*mono_domain_assembly_open_t)(void *, const char *);
static mono_domain_assembly_open_t real_mono_domain_assembly_open = NULL;

// MonoImage* mono_assembly_get_image(MonoAssembly *assembly)
typedef void *(*mono_assembly_get_image_t)(void *);
static mono_assembly_get_image_t real_mono_assembly_get_image = NULL;

// MonoObject* mono_runtime_invoke(MonoMethod *method, void *obj, void **params, MonoObject **exc)
typedef void *(*mono_runtime_invoke_t)(void *, void *, void **, void **);
static mono_runtime_invoke_t real_mono_runtime_invoke = NULL;

/**
 * @brief Load and invoke a clrhook plugin DLL.
 *
 * Attempts to load the specified DLL as a Mono assembly, find the clrhook.Startup class,
 * and invoke its Run method (with or without arguments).
 *
 * @param dll_path Path to the plugin DLL.
 * @param domain Mono domain pointer.
 */
static void clrhook_mono_load_plugin(const char *dll_path, void *domain)
{
    fprintf(stderr, "[clrhook] Loading plugin: %s\n", dll_path);
    void *inject_assembly = real_mono_domain_assembly_open(domain, dll_path);
    if (!inject_assembly)
    {
        fprintf(stderr, "[clrhook] Failed to load %s\n", dll_path);
        return;
    }

    fprintf(stderr, "[clrhook] Loaded assembly: %s\n", dll_path);

    void *image = real_mono_assembly_get_image(inject_assembly);
    if (!image)
    {
        fprintf(stderr, "[clrhook] Failed to get image from assembly %s\n", dll_path);
        return;
    }

    fprintf(stderr, "[clrhook] Got image from assembly: %s\n", dll_path);

    void *klass = real_mono_class_from_name(image, "", "StartupHook");
    if (!klass)
    {
        fprintf(stderr, "[clrhook] Failed to find class StartupHook in %s\n", dll_path);
        return;
    }

    fprintf(stderr, "[clrhook] Found class StartupHook in %s\n", dll_path);

    void *method = real_mono_class_get_method_from_name(klass, "Initialize", 0);
    if (!method)
    {
        fprintf(stderr, "[clrhook] Failed to find method Initialize() in %s\n", dll_path);
        return;
    }

    fprintf(stderr, "[clrhook] Invoking method Initialize() in %s\n", dll_path);

    real_mono_runtime_invoke(method, NULL, NULL, NULL);
}

const char *plugin_dir = "./clrhook/plugins";
const char *parent_dir = "./clrhook";

/*
 * Ensure that the necessary directories exist for the plugin system.
 */
static void clrhook_ensure_directories()
{
    if (!clrhook_ensure_directory(parent_dir, 0755))
        return;
    if (!clrhook_ensure_directory(plugin_dir, 0755))
        return;
}

/**
 * @brief Load all plugin DLLs from the plugin directory for Mono.
 *
 * Scans the plugin directory for .dll files and attempts to load each one as a .NET assembly,
 * invoking their StartupHook.Initialize method if found just as coreclr does with DOTNET_STARTUP_HOOKS.
 *
 * @param native_app_domain Pointer to the Mono application domain.
 */
static void clrhook_mono_load_plugins(void *native_app_domain)
{
    clrhook_ensure_directories();

    DIR *dir = opendir(plugin_dir);
    if (!dir)
    {
        fprintf(stderr, "[clrhook] Failed to open plugin directory: %s\n", plugin_dir);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
        {
            const char *name = entry->d_name;
            size_t len = strlen(name);
            if (len > 4 && strcmp(name + len - 4, ".dll") == 0)
            {
                char dll_path[512];
                snprintf(dll_path, sizeof(dll_path), "%s/%s", plugin_dir, name);
                clrhook_mono_load_plugin(dll_path, native_app_domain);
            }
        }
    }
    closedir(dir);
}

/**
 * @brief Enumeration of supported runtime types.
 */
typedef enum
{
    RUNTIME_UNKNOWN = 0,
    RUNTIME_MONO,
    RUNTIME_CORECLR
} runtime_type_t;

static void *mono_jit_exec_ptr = NULL;
static void *(*real_dlopen)(const char *, int) = NULL;

typedef void (*mono_install_assembly_load_hook_t)(void (*)(void *, void *), void *);
static mono_install_assembly_load_hook_t real_mono_install_assembly_load_hook = NULL;

typedef void *(*mono_domain_get_t)(void);
static mono_domain_get_t real_mono_domain_get = NULL;

typedef const char *(*mono_image_get_name_t)(void *);
static mono_image_get_name_t real_mono_image_get_name = NULL;

/**
 * @brief Check if a given Mono assembly is mscorlib.
 *
 * This is used to skip loading plugins from the mscorlib assembly, which is not a plugin and can cause issues if treated as one.
 *
 * @param assembly Pointer to the Mono assembly to check.
 * @return 1 if the assembly is mscorlib, 0 otherwise.
 */
static int clrhook_is_mscorlib(void *assembly)
{
    void *image = real_mono_assembly_get_image(assembly);
    if (!image)
        return 0;

    const char *name = real_mono_image_get_name(image);
    if (!name)
        return 0;

    if (strcmp(name, "mscorlib") == 0)
        return 1;

    return 0;
}

static int clrhook_assembly_loaded = 0;
/**
 * @brief Mono assembly load hook callback.
 * This function is called by Mono whenever an assembly is loaded and will bootstrap the plugins
 * into the active Mono domain.
 *
 * @param assembly Pointer to the loaded Mono assembly.
 * @param user_data User data pointer (unused in this case).
 */
static void clrhook_mono_on_assembly_load(void *assembly, void *user_data)
{
    (void)user_data;
    if (clrhook_assembly_loaded)
        return;

    void *domain = real_mono_domain_get ? real_mono_domain_get() : NULL;
    if (!domain)
        return;

    // mscorlib is expected to always to be the first assembly loaded by mono.
    // without it loading first System.Object (etc) will not be available to subsequent assemblies.
    if (clrhook_is_mscorlib(assembly))
        return;

    clrhook_assembly_loaded = 1;

    fprintf(stderr, "[clrhook] Mono assembly loaded: %p\n", assembly);

    clrhook_mono_load_plugins(domain);
}

/**
 * @brief Initialize plugin loading for Mono runtime.
 *
 * This function sets up the necessary hooks to load plugins when Mono assemblies are loaded. It retrieves the required Mono API functions and registers the assembly load hook.
 */
static void clrhook_mono_launch()
{
    // mono doesnt have a StartupHooks embedded in the runtime...so lets do it ourselves.
    real_mono_install_assembly_load_hook = (mono_install_assembly_load_hook_t)dlsym(RTLD_DEFAULT, "mono_install_assembly_load_hook");
    real_mono_domain_get = (mono_domain_get_t)dlsym(RTLD_DEFAULT, "mono_domain_get");
    real_mono_image_get_name = (mono_image_get_name_t)dlsym(RTLD_DEFAULT, "mono_image_get_name");
    // plugin loading refs
    real_mono_class_from_name = (mono_class_from_name_t)dlsym(RTLD_DEFAULT, "mono_class_from_name");
    real_mono_class_get_method_from_name = (mono_class_get_method_from_name_t)dlsym(RTLD_DEFAULT, "mono_class_get_method_from_name");
    real_mono_domain_assembly_open = (mono_domain_assembly_open_t)dlsym(RTLD_DEFAULT, "mono_domain_assembly_open");
    real_mono_assembly_get_image = (mono_assembly_get_image_t)dlsym(RTLD_DEFAULT, "mono_assembly_get_image");
    real_mono_runtime_invoke = (mono_runtime_invoke_t)dlsym(RTLD_DEFAULT, "mono_runtime_invoke");

    if (!real_mono_install_assembly_load_hook || !real_mono_domain_get || !real_mono_image_get_name || !real_mono_class_from_name || !real_mono_class_get_method_from_name || !real_mono_domain_assembly_open || !real_mono_assembly_get_image || !real_mono_runtime_invoke)
    {
        fprintf(stderr, "[clrhook] Failed to find one or more Mono API symbols. Plugin loading will not work.\n");
        return;
    }

    // wait until the domain is ready
    real_mono_install_assembly_load_hook(clrhook_mono_on_assembly_load, NULL);
}

/**
 * @brief Initialize plugin loading for CoreCLR runtime.
 *
 * This function scans the plugin directory for .dll files and constructs the DOTNET_STARTUP_HOOKS environment variable to include them, allowing CoreCLR to load them as startup hooks.
 *
 * coreclr has native apis for this: DOTNET_STARTUP_HOOKS
 * see: https://github.com/dotnet/runtime/blob/main/docs/design/features/host-startup-hook.md
 */
static void clrhook_clr_launch()
{
    clrhook_ensure_directories();

    DIR *dir = opendir(plugin_dir);
    if (!dir)
    {
        fprintf(stderr, "[clrhook] Failed to open plugin directory: %s\n", plugin_dir);
        return;
    }

    char hook_list[8192] = {0};
    size_t offset = 0;

    // 1. start with existing DOTNET_STARTUP_HOOKS (if any)
    const char *existing = getenv("DOTNET_STARTUP_HOOKS");
    if (existing && *existing)
    {
        offset += snprintf(hook_list + offset,
                           sizeof(hook_list) - offset,
                           "%s", existing);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN)
            continue;

        const char *name = entry->d_name;
        size_t len = strlen(name);

        if (len <= 4 || strcmp(name + len - 4, ".dll") != 0)
            continue;

        // build temp path first
        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path),
                 "%s/%s", plugin_dir, name);

        // resolve to absolute path (IMPORTANT for CoreCLR)
        char dll_path[512];
        if (!realpath(temp_path, dll_path))
        {
            fprintf(stderr,
                    "[clrhook] realpath failed for %s\n",
                    temp_path);
            continue;
        }

        // 2. append ':' if needed
        if (offset > 0 && hook_list[offset - 1] != ':')
        {
            offset += snprintf(hook_list + offset,
                               sizeof(hook_list) - offset,
                               ":");
        }

        // 3. append plugin path
        offset += snprintf(hook_list + offset,
                           sizeof(hook_list) - offset,
                           "%s",
                           dll_path);
    }

    closedir(dir);

    if (offset == 0)
    {
        fprintf(stderr, "[clrhook] No startup hooks found\n");
        return;
    }

    // 4. set env var (overwrite final merged value)
    setenv("DOTNET_STARTUP_HOOKS", hook_list, 1);

    fprintf(stderr,
            "[clrhook] DOTNET_STARTUP_HOOKS = %s\n",
            hook_list);
}

/**
 * @brief Common startup function for both Mono and CoreCLR.
 *
 * Detects the runtime type and launches the appropriate plugin loading mechanism.
 *
 * @param runtime Detected runtime type.
 */
static void clrhook_startup(runtime_type_t runtime)
{
    fprintf(stderr, "[clrhook] clrhook_startup called\n");
    if (runtime == RUNTIME_MONO)
    {
        fprintf(stderr, "[clrhook] detected Mono runtime\n");
        clrhook_mono_launch();
    }
    else if (runtime == RUNTIME_CORECLR)
    {
        fprintf(stderr, "[clrhook] detected CoreCLR runtime\n");
        clrhook_clr_launch();
    }
    else
    {
        fprintf(stderr, "[clrhook] unknown runtime\n");
    }
}

void *dlopen(const char *filename, int flags)
{
    if (!real_dlopen)
        real_dlopen = dlsym(RTLD_NEXT, "dlopen");

    void *handle = real_dlopen(filename, flags);

    if (!mono_jit_exec_ptr)
    {
        if (filename && strstr(filename, "hostfxr"))
        {
            void *sym = dlsym(handle, "hostfxr_initialize_for_runtime_config");
            if (sym)
            {
                clrhook_startup(RUNTIME_CORECLR);
            }
        }
    }

    return handle;
}

/**
 * @brief clrhook library constructor. Called automatically on library load.
 */
__attribute__((constructor)) static void clrhook_init()
{
    mono_jit_exec_ptr = dlsym(RTLD_DEFAULT, "mono_jit_exec");
    if (mono_jit_exec_ptr)
    {
        clrhook_startup(RUNTIME_MONO);
    }
}

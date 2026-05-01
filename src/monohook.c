#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>

// Verbose logging flag (set via environment variable MONOHOOK_VERBOSE=1)
static int monohook_verbose = -1;

/**
 * @brief Checks and sets the verbose logging flag from the MONOHOOK_VERBOSE environment variable.
 *
 * This function is called before any verbose log output to ensure the flag is initialized.
 */
static void check_verbose() {
    if (monohook_verbose == -1) {
        const char *env = getenv("MONOHOOK_VERBOSE");
        monohook_verbose = (env && strcmp(env, "0") != 0) ? 1 : 0;
    }
}

/**
 * @brief Logs a message to stderr if verbose logging is enabled.
 *
 * Usage: LOGV("format", ...);
 */
#define LOGV(...) do { check_verbose(); if (monohook_verbose) fprintf(stderr, __VA_ARGS__); } while(0)

/**
 * @brief Intercepts execve system calls to log and forward them to the real execve.
 *
 * @param filename Path to the executable.
 * @param argv Argument vector.
 * @param envp Environment vector.
 * @return Result of the real execve, or -1 on failure.
 */
typedef int (*execve_t)(const char *filename, char *const argv[], char *const envp[]);

int execve(const char *filename, char *const argv[], char *const envp[])
{
    static execve_t real_execve = NULL;
    if (!real_execve) {
        real_execve = (execve_t)dlsym(RTLD_NEXT, "execve");
        if (!real_execve) {
            LOGV("[monohook] [ERROR] Failed to resolve real execve!\n");
        } else {
            LOGV("[monohook] Resolved real execve\n");
        }
    }
    LOGV("[monohook] execve called with filename: %s\n", filename);
    return real_execve ? real_execve(filename, argv, envp) : -1;
}

// --- MonoKickstart/Mono dynamic loader detection and injection ---

static int mono_symbols_loaded = 0;

// Function pointer type for mono_jit_exec
// mono_jit_exec(MonoDomain *domain, MonoAssembly *assembly, int argc, char* argv[])
typedef int (*mono_jit_exec_t)(void*, void*, int, char**);
static mono_jit_exec_t real_mono_jit_exec = NULL;

// Function pointer type for mono_domain_assembly_open
// MonoAssembly* mono_domain_assembly_open(MonoDomain *domain, const char *name)
typedef void* (*mono_domain_assembly_open_t)(void*, const char*);
static mono_domain_assembly_open_t real_mono_domain_assembly_open = NULL;

// Function pointer type for mono_get_root_domain
// MonoDomain* mono_get_root_domain(void)
typedef void* (*mono_get_root_domain_t)(void);
static mono_get_root_domain_t real_mono_get_root_domain = NULL;

// Function pointer type for mono_thread_attach
// void* mono_thread_attach(MonoDomain *domain)
typedef void* (*mono_thread_attach_t)(void*);
static mono_thread_attach_t real_mono_thread_attach = NULL;

// Function pointer type for mono_class_from_name, mono_class_get_method_from_name, mono_runtime_invoke
// MonoClass* mono_class_from_name(MonoImage *image, const char* namespace, const char* name)
typedef void* (*mono_class_from_name_t)(void*, const char*, const char*);
static mono_class_from_name_t real_mono_class_from_name = NULL;
// MonoMethod* mono_class_get_method_from_name(MonoClass *klass, const char* name, int param_count)
typedef void* (*mono_class_get_method_from_name_t)(void*, const char*, int);
static mono_class_get_method_from_name_t real_mono_class_get_method_from_name = NULL;
// MonoObject* mono_runtime_invoke(MonoMethod *method, void *obj, void **params, MonoObject **exc)
typedef void* (*mono_runtime_invoke_t)(void*, void*, void**, void**);
static mono_runtime_invoke_t real_mono_runtime_invoke = NULL;
// MonoImage* mono_assembly_get_image(MonoAssembly *assembly)
typedef void* (*mono_assembly_get_image_t)(void*);
static mono_assembly_get_image_t real_mono_assembly_get_image = NULL;

typedef void* (*mono_domain_get_t)(void);
static mono_domain_get_t real_mono_domain_get = NULL;

/**
 * @brief Loads all required Mono runtime symbols using dlsym.
 *
 * Sets global function pointers for Mono API usage. Sets mono_symbols_loaded flag.
 */
static void load_mono_symbols() {
    if (mono_symbols_loaded) return;
    // Try to resolve symbols from the main executable (statically linked Mono)
    real_mono_jit_exec = (mono_jit_exec_t)dlsym(RTLD_DEFAULT, "mono_jit_exec");
    real_mono_domain_assembly_open = (mono_domain_assembly_open_t)dlsym(RTLD_DEFAULT, "mono_domain_assembly_open");
    real_mono_get_root_domain = (mono_get_root_domain_t)dlsym(RTLD_DEFAULT, "mono_get_root_domain");
    real_mono_thread_attach = (mono_thread_attach_t)dlsym(RTLD_DEFAULT, "mono_thread_attach");
    real_mono_class_from_name = (mono_class_from_name_t)dlsym(RTLD_DEFAULT, "mono_class_from_name");
    real_mono_class_get_method_from_name = (mono_class_get_method_from_name_t)dlsym(RTLD_DEFAULT, "mono_class_get_method_from_name");
    real_mono_runtime_invoke = (mono_runtime_invoke_t)dlsym(RTLD_DEFAULT, "mono_runtime_invoke");
    real_mono_assembly_get_image = (mono_assembly_get_image_t)dlsym(RTLD_DEFAULT, "mono_assembly_get_image");
    real_mono_domain_get = (mono_domain_get_t)dlsym(RTLD_DEFAULT, "mono_domain_get");
    // Check if at least one critical symbol is found
    if (real_mono_domain_assembly_open && real_mono_get_root_domain) {
        mono_symbols_loaded = 1;
        LOGV("[monohook] Mono symbols loaded from RTLD_DEFAULT\n");
    } else {
        LOGV("[monohook] [ERROR] Could not resolve Mono symbols from RTLD_DEFAULT\n");
    }
}

// --- Runtime patching for mono_jit_exec ---
static void *orig_mono_jit_exec = NULL;
static unsigned char jit_saved_bytes[16];

// Forward declaration for inject_plugins
static void inject_plugins(void *domain, void *assembly, int argc, char **argv);

/**
 * @brief Finds a static method named "Run" with the specified parameter count in a Mono class.
 *
 * @param klass MonoClass pointer.
 * @param param_count Number of parameters for the method signature.
 * @return Pointer to the MonoMethod, or NULL if not found.
 */
static void* find_run_method(void *klass, int param_count) {
    if (!real_mono_class_get_method_from_name) return NULL;
    return real_mono_class_get_method_from_name(klass, "Run", param_count);
}

// WARNING: This offset is not stable and is based on Mono's internal layout for 64-bit builds.
// Prefer using mono_array_setref or mono_array_addr macros from Mono's API if possible.
#define MONO_ARRAY_DATA_OFFSET 32

/**
 * @brief Creates a managed string[] array from native argv.
 *
 * Allocates a managed array and fills it with managed strings for each argv entry.
 * WARNING: Uses a hardcoded offset for array data. Prefer mono_array_setref if available.
 *
 * @param domain Mono domain pointer.
 * @param image Mono image pointer.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Pointer to the managed array, or NULL on failure.
 */
static void* create_managed_string_array(void *domain, void *image, int argc, char **argv) {
    if (!real_mono_class_from_name || !real_mono_runtime_invoke) {
        LOGV("[monohook] [ERROR] Mono functions for creating string array not found!\n");
        return NULL;
    }

    LOGV("[monohook] Creating managed string array for args: count=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        LOGV("[monohook] - argv[%d]=%s\n", i, argv[i] ? argv[i] : "(null)");
    }

    // Use mono_get_corlib to get the image for System.String
    void* (*mono_get_corlib)() = dlsym(RTLD_DEFAULT, "mono_get_corlib");
    void *corlib_image = mono_get_corlib ? mono_get_corlib() : image;
    void *string_class = real_mono_class_from_name(corlib_image, "System", "String");
    if (!string_class) {
        LOGV("[monohook] [ERROR] Could not find System.String class!\n");
        return NULL;
    }
    void* (*mono_array_new)(void*, void*, uintptr_t) = dlsym(RTLD_DEFAULT, "mono_array_new");
    if (!mono_array_new) {
        LOGV("[monohook] [ERROR] mono_array_new not found!\n");
        return NULL;
    }
    void *arr = mono_array_new(domain, string_class, argc);
    if (!arr) {
        LOGV("[monohook] [ERROR] mono_array_new failed!\n");
        return NULL;
    }
    void* (*mono_string_new)(void*, const char*) = dlsym(RTLD_DEFAULT, "mono_string_new");
    if (!mono_string_new) {
        LOGV("[monohook] [ERROR] mono_string_new not found!\n");
        return NULL;
    }
    // Set array elements directly if mono_array_setref is not available
    void **data = (void**)((char*)arr + MONO_ARRAY_DATA_OFFSET);
    for (int i = 0; i < argc; ++i) {
        const char *src = argv && argv[i] ? argv[i] : "";
        void *str = mono_string_new(domain, src);
        data[i] = str;
        LOGV("[monohook] -> managed[%d]=%s\n", i, src);
    }

    LOGV("[monohook] Created managed string array at %p\n", arr);

    return arr;
}

/**
 * @brief Calls the original mono_jit_exec after restoring its original bytes.
 *
 * @param domain Mono domain pointer.
 * @param assembly Mono assembly pointer.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Result of the original mono_jit_exec, or -1 on failure.
 */
int trampoline_mono_jit_exec(void *domain, void *assembly, int argc, char **argv) {
    if (orig_mono_jit_exec) {
        memcpy(orig_mono_jit_exec, jit_saved_bytes, 16);
        __builtin___clear_cache(orig_mono_jit_exec, (char*)orig_mono_jit_exec + 16);
        int result = ((int (*)(void*, void*, int, char**))orig_mono_jit_exec)(domain, assembly, argc, argv);
        // Optionally re-patch after call
        return result;
    }
    return -1;
}

/**
 * @brief Patched mono_jit_exec entry point. Injects plugins, then calls the original.
 *
 * @param domain Mono domain pointer.
 * @param assembly Mono assembly pointer.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Result of the original mono_jit_exec.
 */
int monohook_mono_jit_exec(void *domain, void *assembly, int argc, char **argv) {
    fprintf(stderr, "[monohook] intercepted mono_jit_exec successfully\n");
    // Pass domain, assembly, argv to inject_plugins
    inject_plugins(domain, assembly, argc, argv);
    return trampoline_mono_jit_exec(domain, assembly, argc, argv);
}

/**
 * @brief Patches the mono_jit_exec function in memory to redirect to monohook_mono_jit_exec.
 *
 * Overwrites the first bytes of mono_jit_exec with a jump to our function.
 */
void patch_mono_jit_exec() {
    static int already_patched = 0;
    if (already_patched) {
        LOGV("[monohook] patch_mono_jit_exec: already patched, skipping\n");
        return;
    }
    void *addr = dlsym(RTLD_DEFAULT, "mono_jit_exec");
    if (!addr) {
        LOGV("[monohook] [ERROR] Could not find mono_jit_exec for patching\n");
        return;
    }
    orig_mono_jit_exec = addr;
    memcpy(jit_saved_bytes, addr, 16);
    uintptr_t page = (uintptr_t)addr & ~(getpagesize() - 1);
    mprotect((void*)page, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC);
    unsigned char patch[16] = {0x48, 0xB8};
    void *target = (void*)monohook_mono_jit_exec;
    memcpy(patch + 2, &target, 8);
    patch[10] = 0xFF; patch[11] = 0xE0;
    memcpy(addr, patch, 12);
    __builtin___clear_cache(addr, (char*)addr + 16);
    LOGV("[monohook] Patched mono_jit_exec at %p -> %p\n", addr, target);
    already_patched = 1;
}

// --- Managed injection, now with plugin directory scan ---
#include <dirent.h>
#include <sys/stat.h>

/**
 * @brief Ensure a directory exists, creating it if necessary.
 *
 * @param path Directory path to check/create.
 * @param mode Permissions to use if creating the directory.
 * @return 1 if the directory exists or was created, 0 on failure.
 */
static int ensure_directory(const char *path, mode_t mode) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, mode) != 0) {
            fprintf(stderr, "[monohook] Failed to create directory: %s\n", path);
            LOGV("[monohook] [ERROR] Could not create directory: %s\n", path);
            return 0;
        }
        LOGV("[monohook] Created directory: %s\n", path);
    }
    return 1;
}

/**
 * @brief Load and invoke a MonoHook plugin DLL.
 *
 * Attempts to load the specified DLL as a Mono assembly, find the MonoHook.Startup class,
 * and invoke its Run method (with or without arguments).
 *
 * @param dll_path Path to the plugin DLL.
 * @param domain Mono domain pointer.
 * @param assembly Mono assembly pointer.
 * @param argc Argument count for managed string array.
 * @param argv Argument vector for managed string array.
 */
static void load_plugin(const char *dll_path, void *domain, void *assembly, int argc, char **argv) {
    LOGV("[monohook] Loading plugin: %s\n", dll_path);
    void *inject_assembly = real_mono_domain_assembly_open(domain, dll_path);
    if (!inject_assembly) {
        fprintf(stderr, "[monohook] Failed to load %s\n", dll_path);
        LOGV("[monohook] [ERROR] mono_domain_assembly_open failed for %s\n", dll_path);
        return;
    }
    LOGV("[monohook] Loaded plugin assembly: %p\n", inject_assembly);
    void *image = real_mono_assembly_get_image(inject_assembly);
    LOGV("[monohook] Got assembly image: %p\n", image);
    void *klass = real_mono_class_from_name(image, "MonoHook", "Startup");
    if (!klass) {
        fprintf(stderr, "[monohook] Failed to find class MonoHook.Startup in %s\n", dll_path);
        LOGV("[monohook] [ERROR] mono_class_from_name failed\n");
        return;
    }
    LOGV("[monohook] Got class: %p\n", klass);
    // Try to find Run(domain, assembly, args) or Run()
    void *method = find_run_method(klass, 3);
    void *params[3] = {0};
    int used_params = 0;
    if (method) {
        void* (*mono_get_corlib)() = dlsym(RTLD_DEFAULT, "mono_get_corlib");
        void *corlib_image = mono_get_corlib ? mono_get_corlib() : image;
        void *appdomain_class = real_mono_class_from_name(corlib_image, "System", "AppDomain");
        void *get_current_domain = NULL;
        if (appdomain_class) {
            get_current_domain = real_mono_class_get_method_from_name(appdomain_class, "get_CurrentDomain", 0);
        }
        void *managed_domain = NULL;
        if (get_current_domain) {
            managed_domain = real_mono_runtime_invoke(get_current_domain, NULL, NULL, NULL);
        } else {
            LOGV("[monohook] [ERROR] Could not find AppDomain.get_CurrentDomain, passing NULL for domain\n");
        }
        void* (*mono_assembly_get_object)(void*, void*) = dlsym(RTLD_DEFAULT, "mono_assembly_get_object");
        void *managed_assembly = NULL;
        if (mono_assembly_get_object) {
            managed_assembly = mono_assembly_get_object(domain, assembly);
        }
        params[0] = managed_domain;
        params[1] = managed_assembly;
        params[2] = create_managed_string_array(domain, image, argc, argv);
        used_params = 3;
        LOGV("[monohook] Using Run(domain, assembly, args) with managed objects\n");
    } else {
        method = find_run_method(klass, 0);
        if (!method) {
            fprintf(stderr, "[monohook] Failed to find method Run (0 or 3 args) in %s\n", dll_path);
            LOGV("[monohook] [ERROR] mono_class_get_method_from_name failed\n");
            return;
        }
        LOGV("[monohook] Using Run()\n");
    }
    real_mono_runtime_invoke(method, NULL, used_params ? params : NULL, NULL);
    LOGV("[monohook] Called mono_runtime_invoke for %s\n", dll_path);
}

/**
 * @brief Scan the plugin directory and load all managed .dll plugins.
 *
 * Ensures the plugin directory exists, then loads each .dll file found using load_plugin().
 *
 * @param domain Mono domain pointer.
 * @param assembly Mono assembly pointer.
 * @param argc Argument count for managed string array.
 * @param argv Argument vector for managed string array.
 */
static void inject_plugins(void *domain, void *assembly, int argc, char **argv) {
    load_mono_symbols();
    if (!mono_symbols_loaded || !real_mono_thread_attach || !real_mono_domain_assembly_open || !real_mono_assembly_get_image || !real_mono_class_from_name || !real_mono_class_get_method_from_name || !real_mono_runtime_invoke) {
        fprintf(stderr, "[monohook] Mono symbols not found, skipping inject.\n");
        LOGV("[monohook] [ERROR] One or more Mono symbols missing.\n");
        return;
    }
    if (!domain) {
        fprintf(stderr, "[monohook] Could not get Mono domain.\n");
        LOGV("[monohook] [ERROR] Could not get Mono domain.\n");
        return;
    }
    LOGV("[monohook] Got Mono domain: %p\n", domain);
    real_mono_thread_attach(domain);
    LOGV("[monohook] Attached thread to domain\n");

    // Scan plugins directory for .dll files
    const char *plugin_dir = "./monohook/plugins";
    const char *parent_dir = "./monohook";
    if (!ensure_directory(parent_dir, 0755)) return;
    if (!ensure_directory(plugin_dir, 0755)) return;
    
    DIR *dir = opendir(plugin_dir);
    if (!dir) {
        fprintf(stderr, "[monohook] Failed to open plugin directory: %s\n", plugin_dir);
        LOGV("[monohook] [ERROR] Could not open plugin directory\n");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            const char *name = entry->d_name;
            size_t len = strlen(name);
            if (len > 4 && strcmp(name + len - 4, ".dll") == 0) {
                char dll_path[512];
                snprintf(dll_path, sizeof(dll_path), "%s/%s", plugin_dir, name);
                load_plugin(dll_path, domain, assembly, argc, argv);
            }
        }
    }
    closedir(dir);
}

/**
 * @brief MonoHook library constructor. Called automatically on library load.
 *
 * Patches mono_jit_exec to enable plugin injection.
 */
__attribute__((constructor))
static void monohook_init() {
    patch_mono_jit_exec();
}

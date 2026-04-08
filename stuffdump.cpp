#include <jni.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

#define TAG "stuffdump"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── IL2CPP export function pointers ──────────────────────────────────────────
static void*  (*il2cpp_domain_get)()                                              = nullptr;
static void*  (*il2cpp_domain_assembly_open)(void*, const char*)                  = nullptr;
static void*  (*il2cpp_assembly_get_image)(void*)                                 = nullptr;
static void*  (*il2cpp_class_from_name)(void*, const char*, const char*)          = nullptr;
static void*  (*il2cpp_class_get_type)(void*)                                     = nullptr;
static void*  (*il2cpp_type_get_object)(void*)                                    = nullptr;

static void* libil2cpp_base = nullptr;

void* get_rva(uint64_t rva) {
    return (uint8_t*)libil2cpp_base + rva;
}

// ── String helpers ────────────────────────────────────────────────────────────
std::string read_il2cpp_string(void* strPtr) {
    if (!strPtr) return "";
    int32_t len = *(int32_t*)((uint8_t*)strPtr + 0x10);
    if (len <= 0 || len > 1024) return "";
    uint16_t* chars = (uint16_t*)((uint8_t*)strPtr + 0x14);
    std::string result;
    for (int i = 0; i < len; i++)
        result += (chars[i] < 128) ? (char)chars[i] : '?';
    return result;
}

template<typename T>
T read_at(void* base, size_t offset) {
    return *(T*)((uint8_t*)base + offset);
}

int32_t array_len(void* arr) {
    return arr ? read_at<int32_t>(arr, 0x18) : 0;
}

void* array_elem(void* arr, int i) {
    return arr ? read_at<void*>(arr, 0x20 + i * 8) : nullptr;
}

// ── FindObjectOfType/FindObjectsOfType via RVA ────────────────────────────────
typedef void* (*FOT_fn)(void*);
typedef void* (*FOTS_fn)(void*);

void* find_object_of_type(void* cls) {
    if (!il2cpp_class_get_type || !il2cpp_type_get_object) return nullptr;
    void* typePtr = il2cpp_class_get_type(cls);
    void* typeObj = il2cpp_type_get_object(typePtr);
    auto fn = (FOT_fn)get_rva(0x46B78D8);
    return fn(typeObj);
}

void* find_objects_of_type(void* cls) {
    if (!il2cpp_class_get_type || !il2cpp_type_get_object) return nullptr;
    void* typePtr = il2cpp_class_get_type(cls);
    void* typeObj = il2cpp_type_get_object(typePtr);
    auto fn = (FOTS_fn)get_rva(0x46B7684);
    return fn(typeObj);
}

// ── Dump ──────────────────────────────────────────────────────────────────────
void do_dump(std::ostringstream& out) {
    out << "=== stuffdump v1.0 ===\n\n";

    auto domain = il2cpp_domain_get();
    if (!domain) { out << "ERROR: domain null\n"; return; }

    auto asm_cs = il2cpp_domain_assembly_open(domain, "Assembly-CSharp");
    if (!asm_cs) { out << "ERROR: Assembly-CSharp not found\n"; return; }

    auto img = il2cpp_assembly_get_image(asm_cs);
    if (!img) { out << "ERROR: image null\n"; return; }

    // ── 1. MapItemGenerator.baseItems[].PathToItem ────────────────────────────
    out << "--- MapItemGenerator.baseItems[].PathToItem ---\n";
    auto mgClass = il2cpp_class_from_name(img, "", "MapItemGenerator");
    if (!mgClass) { out << "  MapItemGenerator: class not found\n"; }
    else {
        void* mgr = find_object_of_type(mgClass);
        if (!mgr) { out << "  MapItemGenerator: not in scene\n"; }
        else {
            void* baseItems = read_at<void*>(mgr, 0x30);
            int32_t len = array_len(baseItems);
            out << "  count: " << len << "\n";
            for (int i = 0; i < len; i++) {
                void* item = array_elem(baseItems, i);
                if (!item) continue;
                void* strPtr = read_at<void*>(item, 0x10);
                std::string path = read_il2cpp_string(strPtr);
                out << "  [" << i << "] " << path << "\n";
            }
        }
    }
    out << "\n";

    // ── 2. SpawnItemZone items[].itemName ─────────────────────────────────────
    out << "--- SpawnItemZone.items[].itemName ---\n";
    auto szClass = il2cpp_class_from_name(img, "", "SpawnItemZone");
    if (!szClass) { out << "  SpawnItemZone: class not found\n"; }
    else {
        void* zones = find_objects_of_type(szClass);
        int32_t zcount = array_len(zones);
        out << "  zones in scene: " << zcount << "\n";
        std::vector<std::string> seen;
        for (int z = 0; z < zcount; z++) {
            void* zone = array_elem(zones, z);
            if (!zone) continue;
            // List<SpawnableItem> items at 0x28
            void* list     = read_at<void*>(zone, 0x28);
            if (!list) continue;
            void* listArr  = read_at<void*>(list, 0x10);
            int32_t lsize  = read_at<int32_t>(list, 0x18);
            if (!listArr) continue;
            for (int i = 0; i < lsize; i++) {
                void* si = array_elem(listArr, i);
                if (!si) continue;
                void* strPtr = read_at<void*>(si, 0x10);
                std::string name = read_il2cpp_string(strPtr);
                if (name.empty()) continue;
                bool dup = false;
                for (auto& s : seen) if (s == name) { dup = true; break; }
                if (!dup) { seen.push_back(name); out << "  " << name << "\n"; }
            }
        }
    }
    out << "\n";

    // ── 3. ItemManager.curentPath from live items ─────────────────────────────
    out << "--- ItemManager.curentPath (live spawned items) ---\n";
    auto imClass = il2cpp_class_from_name(img, "", "ItemManager");
    if (!imClass) { out << "  ItemManager: class not found\n"; }
    else {
        void* items = find_objects_of_type(imClass);
        int32_t ilen = array_len(items);
        out << "  live items: " << ilen << "\n";
        std::vector<std::string> seen;
        for (int i = 0; i < ilen; i++) {
            void* item = array_elem(items, i);
            if (!item) continue;
            void* strPtr = read_at<void*>(item, 0x30);
            std::string path = read_il2cpp_string(strPtr);
            if (path.empty()) continue;
            bool dup = false;
            for (auto& s : seen) if (s == path) { dup = true; break; }
            if (!dup) { seen.push_back(path); out << "  " << path << "\n"; }
        }
    }
    out << "\n";

    // ── 4. EntitySceneManager.entityColections[].EntityResourcesName ──────────
    out << "--- EntitySceneManager.entityColections[].EntityResourcesName ---\n";
    auto esmClass = il2cpp_class_from_name(img, "", "EntitySceneManager");
    if (!esmClass) { out << "  EntitySceneManager: class not found\n"; }
    else {
        void* esm = find_object_of_type(esmClass);
        if (!esm) { out << "  EntitySceneManager: not in scene\n"; }
        else {
            void* colls = read_at<void*>(esm, 0x30);
            int32_t clen = array_len(colls);
            out << "  count: " << clen << "\n";
            for (int i = 0; i < clen; i++) {
                void* coll = array_elem(colls, i);
                if (!coll) continue;
                void* strPtr = read_at<void*>(coll, 0x10);
                std::string name = read_il2cpp_string(strPtr);
                out << "  [" << i << "] " << name << "\n";
            }
        }
    }
    out << "\n";

    // ── 5. ShamcerManager field verification ──────────────────────────────────
    out << "--- ShamcerManager field offsets (from dump.cs, confirmed) ---\n";
    out << "  0x88  int   maxBullets\n";
    out << "  0x8C  float shootTimer_sec\n";
    out << "  0x98  int   bullets_holder\n";
    out << "  0x9C  bool  shootKD\n";
    out << "  this = x19 register (confirmed via Frida register dump)\n\n";

    out << "--- ShotPungManager field offsets ---\n";
    out << "  0x40  bool  ToShoots\n";
    out << "  0x98  bool  blocking\n";
    out << "  0x9C  int   ShootingCount\n";
    out << "  this = x19 register\n\n";

    out << "--- ClientGameManager currency offsets ---\n";
    out << "  0xD0  int   playerBalance (silver)\n";
    out << "  0xE4  int   goldBalance\n";
    out << "  0xF0  ptr   selfClientPlayer\n\n";

    out << "=== end of dump ===\n";
}

// ── Background thread ─────────────────────────────────────────────────────────
static void* dump_thread(void*) {
    sleep(5); // wait for il2cpp init

    // Find libil2cpp.so base + full path
    char il2cpp_path[512] = {};
    FILE* maps = fopen("/proc/self/maps", "r");
    char line[512];
    while (maps && fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
            uint64_t base = 0;
            sscanf(line, "%llx", (unsigned long long*)&base);
            libil2cpp_base = (void*)base;
            char* p = strrchr(line, ' ');
            if (!p) p = strrchr(line, '\t');
            if (p) { strncpy(il2cpp_path, p+1, sizeof(il2cpp_path)-1); il2cpp_path[strcspn(il2cpp_path,"\n")] = 0; }
            LOGI("base=%p path=%s", libil2cpp_base, il2cpp_path);
            break;
        }
    }
    if (maps) fclose(maps);

    if (!libil2cpp_base) { LOGE("libil2cpp.so not found"); return nullptr; }

    // Resolve IL2CPP exports — try dlopen first, fall back to RVA offsets
    void* h = dlopen(il2cpp_path[0] ? il2cpp_path : "libil2cpp.so", RTLD_NOLOAD | RTLD_NOW | RTLD_GLOBAL);
    LOGI("dlopen handle = %p", h);
    void* src = h ? h : RTLD_DEFAULT;

    il2cpp_domain_get           = (decltype(il2cpp_domain_get))          dlsym(src, "il2cpp_domain_get");
    il2cpp_domain_assembly_open = (decltype(il2cpp_domain_assembly_open))dlsym(src, "il2cpp_domain_assembly_open");
    il2cpp_assembly_get_image   = (decltype(il2cpp_assembly_get_image))  dlsym(src, "il2cpp_assembly_get_image");
    il2cpp_class_from_name      = (decltype(il2cpp_class_from_name))     dlsym(src, "il2cpp_class_from_name");
    il2cpp_class_get_type       = (decltype(il2cpp_class_get_type))      dlsym(src, "il2cpp_class_get_type");
    il2cpp_type_get_object      = (decltype(il2cpp_type_get_object))     dlsym(src, "il2cpp_type_get_object");

    if (!il2cpp_domain_get) {
        LOGI("dlsym failed, resolving via RVA from base");
        // Export RVAs from Frida-Map.js — offset from libil2cpp.so base
        auto b = (uint8_t*)libil2cpp_base;
        il2cpp_domain_get           = (decltype(il2cpp_domain_get))          (b + 0x4A0E8E0);
        il2cpp_domain_assembly_open = (decltype(il2cpp_domain_assembly_open))(b + 0x4A0E5C0);
        il2cpp_assembly_get_image   = (decltype(il2cpp_assembly_get_image))  (b + 0x4A0D6A0);
        il2cpp_class_from_name      = (decltype(il2cpp_class_from_name))     (b + 0x4A0DC50);
        il2cpp_class_get_type       = (decltype(il2cpp_class_get_type))      (b + 0x4A15990);
        il2cpp_type_get_object      = (decltype(il2cpp_type_get_object))     (b + 0x4A15AB0);
        LOGI("using RVA fallback for exports");
    }
    LOGI("il2cpp_domain_get = %p", (void*)il2cpp_domain_get);

    // Wait for room
    typedef bool (*InRoom_fn)();
    auto in_room = (InRoom_fn)get_rva(0x3CA6588);
    LOGI("waiting for room...");
    for (int i = 0; i < 120; i++) {
        if (in_room && in_room()) { LOGI("in room! dumping..."); break; }
        sleep(1);
    }
    sleep(2);

    std::ostringstream out;
    do_dump(out);

    // Write file
    const char* path = "/sdcard/Android/data/com.stuffhorror/files/stuffdump.txt";
    // Try multiple paths
    const char* paths[] = {
        "/sdcard/stuffdump.txt",
        "/sdcard/Android/data/com.stuffhorror/files/stuffdump.txt",
        "/data/local/tmp/stuffdump.txt",
        nullptr
    };

    bool written = false;
    for (int i = 0; paths[i]; i++) {
        std::ofstream f(paths[i]);
        if (f.is_open()) {
            f << out.str();
            f.close();
            LOGI("written to %s", paths[i]);
            written = true;
            break;
        }
    }
    if (!written) LOGE("failed to write any output file");

    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void* init(void*) {
    LOGI("stuffdump loaded!");
    pthread_t t;
    pthread_create(&t, nullptr, dump_thread, nullptr);
    pthread_detach(t);
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;
    init(nullptr);
    return JNI_VERSION_1_6;
}

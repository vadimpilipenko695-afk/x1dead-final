#include "core.hpp"
#include <jni.h>
#include "hooking.hpp"
#include "../includes/imgui/imgui.h"
#include "../includes/imgui/backends/imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern "C" {
__attribute__((visibility("hidden"), used)) void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    unsigned char v = (unsigned char)c;
    while (n--) *p++ = v;
    return s;
}
__attribute__((visibility("hidden"), used)) void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    while (n--) *dd++ = *ss++;
    return d;
}
__attribute__((visibility("hidden"), used)) void* memmove(void* d, const void* s, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
__attribute__((visibility("hidden"), used)) int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
__attribute__((visibility("hidden"), used)) void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned char v = (unsigned char)c;
    while (n--) { if (*p == v) return (void*)p; p++; }
    return nullptr;
}
__attribute__((visibility("hidden"), used)) size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
__attribute__((visibility("hidden"), used)) int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
__attribute__((visibility("hidden"), used)) int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0;
}
__attribute__((visibility("hidden"), used)) char* strstr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char* a = h;
        const char* b = n;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return nullptr;
}
}

struct lib_seg { uint64_t va, foff, size, vaddr; bool exec, forge; };
static std::vector<lib_seg> g_segs;
static std::once_flag g_segs_once;

static void build_segs() {
    g_segs.clear();
    size_t n = 0;
    char* buf = load_maps_full(&n);
    if (!buf) return;
    char* p = buf;
    while (*p) {
        char* le = p;
        while (*le && *le != '\n') le++;
        if (*le) *le = 0;
        uint64_t st = 0, en = 0, off = 0;
        int k = 0;
        while (ishex(p[k])) { st = st * 16 + (uint64_t)hexv(p[k]); k++; }
        if (p[k] == '-') {
            k++;
            while (ishex(p[k])) { en = en * 16 + (uint64_t)hexv(p[k]); k++; }
        }
        while (p[k] == ' ') k++;
        bool exec = (p[k] == 'r' && p[k + 2] == 'x');
        for (int i = 0; i < 4 && p[k]; i++) k++;
        while (p[k] == ' ') k++;
        while (ishex(p[k])) { off = off * 16 + (uint64_t)hexv(p[k]); k++; }
        const char* q = p;
        while (*q && *q != '/') q++;
        if (*q == '/' && strstr(q, "libunity.so") && en > st)
            g_segs.push_back({st, off, en - st, 0, exec, false});
        p = le + 1;
    }
    free(buf);

    uint64_t max_end = 0;
    for (auto& s : g_segs)
        if (s.foff + s.size > max_end) max_end = s.foff + s.size;

    uint64_t load_bias = 0;
    for (auto& s : g_segs) {
        if (s.foff == 0) {
            if (max_end && s.foff + s.size == max_end) s.forge = true;
            if (!s.forge) load_bias = s.va;
        }
    }
    if (!load_bias)
        for (auto& s : g_segs)
            if (s.foff == 0 && !s.forge && (!load_bias || s.va < load_bias)) load_bias = s.va;
    if (!load_bias && !g_segs.empty()) load_bias = g_segs[0].va;

    for (auto& s : g_segs)
        if (!s.forge) s.vaddr = s.va - load_bias;
}

static void* segment_resolve_rva(uint64_t rva) {
    std::call_once(g_segs_once, build_segs);
    void* any = nullptr;
    for (size_t i = 0; i < g_segs.size(); i++) {
        const lib_seg& s = g_segs[i];
        if (s.forge || !s.size) continue;
        if (rva < s.vaddr || rva >= s.vaddr + s.size) continue;
        void* a = (void*)(s.va + (rva - s.vaddr));
        if (s.exec) return a;
        if (!any) any = a;
    }
    return any;
}

struct Touch {
    int32_t finger;
    float px, py, rx, ry, dx, dy;
    float timedelta;
    int32_t tapcount;
    int32_t phase;
    int32_t type;
    float pressure;
    float maxpressure;
    float radius;
    float radiusvar;
    float altangle;
    float azangle;
    float pad[16];
};

static int (*touch_count_fn)();
static struct Touch (*get_touch_fn)(int);

static bool touch_init() {
    il2cpp::init_api(g_base);
    if (!il2cpp::domain_get || !il2cpp::domain_assembly_open || !il2cpp::assembly_get_image ||
        !il2cpp::class_from_name || !il2cpp::class_get_method_from_name) return false;
    Il2CppDomain* dom = il2cpp::domain_get();
    if (!dom) return false;
    if (il2cpp::thread_attach) il2cpp::thread_attach(dom);

    const char* asm_names[] = { "UnityEngine.CoreModule", "UnityEngine.InputLegacyModule", "UnityEngine.InputModule" };
    for (int a = 0; a < 3; a++) {
        Il2CppAssembly* asm_ = il2cpp::domain_assembly_open(dom, asm_names[a]);
        if (!asm_) continue;
        Il2CppImage* img = il2cpp::assembly_get_image(asm_);
        if (!img) continue;
        Il2CppClass* input = il2cpp::class_from_name(img, "UnityEngine", "Input");
        if (!input) continue;
        const Il2CppMethod* mc = il2cpp::class_get_method_from_name(input, "get_touchCount", 0);
        const Il2CppMethod* mt = il2cpp::class_get_method_from_name(input, "GetTouch", 1);
        if (!mc) mc = find_method(input, "get_touchCount");
        if (!mt) mt = find_method(input, "GetTouch");
        if (!mc || !mt) continue;
        void* pc = *(void**)((uintptr_t)mc + il2cpp::offset::il2cpp_method_pointer);
        void* pt = *(void**)((uintptr_t)mt + il2cpp::offset::il2cpp_method_pointer);
        if (!pc || !pt) continue;
        touch_count_fn = (int (*)())pc;
        get_touch_fn = (struct Touch (*)(int))pt;
        return true;
    }
    return false;
}

static void handle_touch() {
    ImGuiIO& io = ImGui::GetIO();
    if (!touch_count_fn || !get_touch_fn) return;
    int n = touch_count_fn();
    static bool active = false;
    if (n <= 0) {
        if (active) { io.MouseDown[0] = false; active = false; }
        return;
    }
    for (int i = 0; i < n; i++) {
        struct Touch t = get_touch_fn(i);
        int ph = t.phase;
        float x = t.px;
        float y = (float)scr_h - t.py;
        if (ph == 0 || ph == 1 || ph == 2) {
            io.MousePos = ImVec2(x, y);
            io.MouseDown[0] = true;
            active = true;
        } else {
            io.MouseDown[0] = false;
            active = false;
        }
    }
}

struct lf_s { const char* name; uintptr_t addr; };
static int lf_cb(struct dl_phdr_info* i, size_t s, void* d) {
    (void)s;
    lf_s* f = (lf_s*)d;
    if (i->dlpi_name && strstr(i->dlpi_name, f->name)) { f->addr = i->dlpi_addr; return 1; }
    return 0;
}
static uintptr_t find_lib(const char* n) {
    lf_s f{n, 0};
    dl_iterate_phdr(lf_cb, &f);
    return f.addr;
}

static uintptr_t pick_base() {
    size_t n = 0;
    char* buf = load_maps_full(&n);
    if (buf) {
        char* p = buf;
        while (*p) {
            char* le = p;
            while (*le && *le != '\n') le++;
            if (*le) *le = 0;
            uint64_t st = 0, en = 0;
            int k = 0;
            while (ishex(p[k])) { st = st * 16 + (uint64_t)hexv(p[k]); k++; }
            if (p[k] == '-') {
                k++;
                while (ishex(p[k])) { en = en * 16 + (uint64_t)hexv(p[k]); k++; }
            }
            while (p[k] == ' ') k++;
            if (p[k] == 'r' && p[k + 3] == 'p') {
                const char* q = p;
                while (*q && *q != '/') q++;
                if (*q == '/' && strstr(q, "libunity.so") && (en - st) < 0x5100000) {
                    free(buf);
                    return st;
                }
            }
            p = le + 1;
        }
        free(buf);
    }
    return find_lib("libunity.so");
}

static void render_frame() {
    static bool ready = false;
    if (!ready) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)scr_w, (float)scr_h);
        io.IniFilename = nullptr;
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(pixeloperator, sizeof(pixeloperator), 30.f, &fc, io.Fonts->GetGlyphRangesCyrillic());
        watermark::init();
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ready = true;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x != (float)scr_w || io.DisplaySize.y != (float)scr_h)
        io.DisplaySize = ImVec2((float)scr_w, (float)scr_h);
    ImGui_ImplOpenGL3_NewFrame();
    handle_touch();
    ImGui::NewFrame();
    menu::render();
    events::fire_render();
    watermark::render();
    ImGui::EndFrame();
    ImGui::Render();
    glViewport(0, 0, scr_w, scr_h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface);

static EGLBoolean hk_swap(EGLDisplay d, EGLSurface s) {
    EGLint w = 0, h = 0;
    if (d && s) {
        eglQuerySurface(d, s, EGL_WIDTH, &w);
        eglQuerySurface(d, s, EGL_HEIGHT, &h);
    }
    if (w > 0 && h > 0) { scr_w = w; scr_h = h; }
    std::lock_guard<std::mutex> lg(g_mtx);
    render_frame();
    return orig_swap(d, s);
}

static void* thread_main(void*) {
    il2cpp::resolve_rva = segment_resolve_rva;
    build_maps();
    g_base = pick_base();
    while (!g_base) {
        sleep(1);
        build_maps();
        g_base = pick_base();
    }
    LOG("g_base=%llx", (unsigned long long)g_base);
    hooking::pump();
    void* egl = nullptr;
    void* eh = dlopen("libEGL.so", RTLD_NOW);
    if (eh) {
        egl = dlsym(eh, "eglSwapBuffers");
    }
    if (!egl) egl = dlsym(RTLD_NEXT, "eglSwapBuffers");
    if (!egl) egl = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (egl) {
        inline_hook(egl, (void*)hk_swap, (void**)&orig_swap);
    }

    while (true) {
        sleep(2);
        build_maps();
        hooking::pump();
        if (!touch_count_fn || !get_touch_fn) touch_init();
        uint64_t new_base = pick_base();
        if (new_base && new_base != g_base) {
            LOG("base change %llx -> %llx, resetting hooks",
                (unsigned long long)g_base, (unsigned long long)new_base);
            g_base = new_base;
            il2cpp::init_api(g_base);
            hooking::reset();
        }
    }
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void payload_entry(void* base_addr) {
    (void)base_addr;
    il2cpp::resolve_rva = segment_resolve_rva;
    pthread_t th;
    if (pthread_create(&th, nullptr, thread_main, nullptr) == 0)
        pthread_detach(th);
}

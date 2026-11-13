// main.cpp — exodium internal, ARM64 Android C++20
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <atomic>

#define IMGUI_IMPL_OPENGL_ES3
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "menu/menu.h"
#include "menu/boot.h"
#include "menu/fonts.h"
#include "gl_state.h"
#include "antidet.h"

#define TAG "dvm"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)

using SwapFn = EGLBoolean(*)(EGLDisplay,EGLSurface);
static void*  g_tramp = nullptr;
static SwapFn g_orig  = nullptr;
static std::atomic<bool> g_hooked{false};
static std::atomic<bool> g_init{false};
static nl::Menu g_menu;
static EGLint g_w=0, g_h=0;
static int g_frames=0;

static bool do_hook(void* target, void* replace) {
    g_tramp = mmap(nullptr,64,PROT_READ|PROT_WRITE|PROT_EXEC,
                   MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    if(g_tramp==MAP_FAILED){LOGI("tramp mmap fail");return false;}
    memcpy(g_tramp,target,16);
    uintptr_t back=(uintptr_t)target+16;
    uint8_t jb[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1F,0xD6,
        (uint8_t)back,(uint8_t)(back>>8),(uint8_t)(back>>16),(uint8_t)(back>>24),
        (uint8_t)(back>>32),(uint8_t)(back>>40),(uint8_t)(back>>48),(uint8_t)(back>>56)};
    memcpy((uint8_t*)g_tramp+16,jb,16);
    __builtin___clear_cache((char*)g_tramp,(char*)g_tramp+32);
    g_orig=reinterpret_cast<SwapFn>(g_tramp);
    uintptr_t r=(uintptr_t)replace;
    uint8_t p[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1F,0xD6,
        (uint8_t)r,(uint8_t)(r>>8),(uint8_t)(r>>16),(uint8_t)(r>>24),
        (uint8_t)(r>>32),(uint8_t)(r>>40),(uint8_t)(r>>48),(uint8_t)(r>>56)};
    int fd=open("/proc/self/mem",O_RDWR);
    if(fd>=0){
        ssize_t w=pwrite(fd,p,16,(off_t)(uintptr_t)target);
        close(fd); LOGI("pwrite %zd/16 to %p",w,target);
        if(w!=16) return false;
    } else {
        uintptr_t pg=(uintptr_t)target&~0xFFFULL;
        mprotect((void*)pg,0x2000,7); memcpy(target,p,16);
        LOGI("mprotect fallback");
    }
    __builtin___clear_cache((char*)target,(char*)target+16);
    return true;
}

static EGLBoolean hooked_swap(EGLDisplay,EGLSurface) {
    EGLContext ctx=eglGetCurrentContext();
    EGLDisplay d=eglGetCurrentDisplay();
    EGLSurface s=eglGetCurrentSurface(EGL_DRAW);
    if(ctx==EGL_NO_CONTEXT) return g_orig(d,s);
    if(s!=EGL_NO_SURFACE){
        EGLint w=0,h=0;
        eglQuerySurface(d,s,EGL_WIDTH,&w);
        eglQuerySurface(d,s,EGL_HEIGHT,&h);
        if(w>0&&h>0){g_w=w;g_h=h;}
    }
    if(!g_init.load()&&g_w>0){
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io=ImGui::GetIO();
        io.DisplaySize=ImVec2((float)g_w,(float)g_h);
        io.IniFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
        ImGui_ImplOpenGL3_Init("#version 300 es");
        nl::load_fonts();
        g_menu.init(ImVec2(80.f,80.f),1.0f);
        g_menu.set_visible(true);
        nl::boot::notify("Exodium ready");
        g_init.store(true);
        LOGI("imgui init %dx%d",g_w,g_h);
    }
    if(!g_init.load()) return g_orig(d,s);
    ImGui::GetIO().DisplaySize=ImVec2((float)g_w,(float)g_h);
    GlState gl; gl.save();
    glViewport(0,0,g_w,g_h);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    nl::boot::notifications();
    g_menu.render();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    gl.restore();
    if(++g_frames<=5) LOGI("frame %d %dx%d",g_frames,g_w,g_h);
    return g_orig(d,s);
}

static void* startup(void*) {
    prctl(PR_SET_NAME,"RenderThread",0,0,0);
    antidet::install();
    LOGI("sleeping 40s");
    sleep(40);
    static const char* paths[]={
        "libEGL.so","/system/lib64/libEGL.so",
        "/vendor/lib64/libEGL.so",
        "/vendor/lib64/egl/libEGL_adreno.so",
        "/apex/com.android.opengl/lib64/libEGL.so",nullptr
    };
    void* fn=nullptr;
    for(int i=0;paths[i]&&!fn;i++){
        void* h=dlopen(paths[i],RTLD_NOW|RTLD_GLOBAL|RTLD_NOLOAD);
        if(!h) h=dlopen(paths[i],RTLD_NOW|RTLD_GLOBAL);
        LOGI("dlopen %s -> %p",paths[i],h);
        if(!h) continue;
        fn=dlsym(h,"eglSwapBuffers");
        LOGI("eglSwapBuffers -> %p",fn);
    }
    if(!fn){
        FILE* f=fopen("/proc/self/maps","r"); char line[512];
        while(f&&fgets(line,512,f)&&!fn){
            if(!strstr(line,"libEGL")) continue;
            char* p=strrchr(line,'/'); if(!p) continue; p++;
            char* nl=strchr(p,'\n'); if(nl)*nl=0;
            char full[256]; snprintf(full,sizeof(full),"%s",p);
            void* h=dlopen(full,RTLD_NOW|RTLD_GLOBAL|RTLD_NOLOAD);
            if(!h) continue;
            fn=dlsym(h,"eglSwapBuffers");
        }
        if(f) fclose(f);
    }
    if(!fn){LOGI("eglSwapBuffers not found");return nullptr;}
    if(!do_hook(fn,(void*)hooked_swap)){LOGI("hook failed");return nullptr;}
    g_hooked.store(true);
    LOGI("hook live");
    return nullptr;
}

__attribute__((constructor))
static void on_load(){
    LOGI("on_load");
    pthread_t t; pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED);
    pthread_create(&t,&a,startup,nullptr);
    pthread_attr_destroy(&a);
}

// injector.c — ARM64 Android ptrace injector with memfd anti-ban
// Language: C11  Requires: root (KernelSU)
// Usage: ./injector <pid|package> <so_path>
// Anti-ban: loads .so via memfd (maps show anonymous), unlinks file after inject

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <stdint.h>

typedef struct {
    unsigned long long regs[31];
    unsigned long long sp, pc, pstate;
} arm64_regs;

static uintptr_t module_base(pid_t pid, const char* name) {
    char path[64]; snprintf(path,sizeof(path),"/proc/%d/maps",pid);
    FILE* f=fopen(path,"r"); if(!f) return 0;
    char line[512]; uintptr_t base=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strstr(line,name)) continue;
        base=(uintptr_t)strtoull(line,NULL,16); break;
    }
    fclose(f); return base;
}

static int mem_read(pid_t pid,uintptr_t addr,void* buf,size_t len) {
    char p[64]; snprintf(p,sizeof(p),"/proc/%d/mem",pid);
    int fd=open(p,O_RDONLY); if(fd<0) return -1;
    ssize_t r=pread(fd,buf,len,(off_t)addr); close(fd);
    return r==(ssize_t)len?0:-1;
}
static int mem_write(pid_t pid,uintptr_t addr,const void* buf,size_t len) {
    char p[64]; snprintf(p,sizeof(p),"/proc/%d/mem",pid);
    int fd=open(p,O_WRONLY); if(fd<0) return -1;
    ssize_t w=pwrite(fd,buf,len,(off_t)addr); close(fd);
    return w==(ssize_t)len?0:-1;
}

static pid_t find_pid(const char* pkg) {
    DIR* d=opendir("/proc"); if(!d) return -1;
    struct dirent* e;
    while((e=readdir(d))) {
        char* end; long pid=strtol(e->d_name,&end,10); if(*end) continue;
        char pp[64]; snprintf(pp,sizeof(pp),"/proc/%ld/cmdline",pid);
        FILE* f=fopen(pp,"r"); if(!f) continue;
        char cmd[256]={};  fread(cmd,1,255,f); fclose(f);
        if(strstr(cmd,pkg)){closedir(d);return(pid_t)pid;}
    }
    closedir(d); return -1;
}

static uintptr_t find_svc(pid_t pid, uintptr_t base) {
    uint8_t buf[0x1000];
    for(uintptr_t off=0;off<0x20000;off+=sizeof(buf)) {
        if(mem_read(pid,base+off,buf,sizeof(buf))<0) break;
        for(size_t i=0;i+4<=sizeof(buf);i+=4) {
            uint32_t insn; memcpy(&insn,buf+i,4);
            if(insn==0xD4000001) return base+off+i; // svc #0
        }
    }
    return 0;
}

// Execute a syscall in target process via ptrace
// Returns x0 (syscall result)
static uintptr_t remote_syscall(pid_t pid, uintptr_t svc_addr,
    uintptr_t x8,
    uintptr_t x0,uintptr_t x1,uintptr_t x2,
    uintptr_t x3,uintptr_t x4,uintptr_t x5)
{
    struct iovec iov;
    arm64_regs regs, saved;
    iov.iov_base=&regs; iov.iov_len=sizeof(regs);
    if(ptrace(PTRACE_GETREGSET,pid,(void*)1,&iov)<0) return (uintptr_t)-1;
    memcpy(&saved,&regs,sizeof(regs));

    regs.regs[8]=x8;
    regs.regs[0]=x0; regs.regs[1]=x1; regs.regs[2]=x2;
    regs.regs[3]=x3; regs.regs[4]=x4; regs.regs[5]=x5;
    regs.pc=svc_addr;
    iov.iov_base=&regs; ptrace(PTRACE_SETREGSET,pid,(void*)1,&iov);
    ptrace(PTRACE_SINGLESTEP,pid,NULL,NULL);
    int st; waitpid(pid,&st,0);
    iov.iov_base=&regs; iov.iov_len=sizeof(regs);
    ptrace(PTRACE_GETREGSET,pid,(void*)1,&iov);
    uintptr_t result=regs.regs[0];

    iov.iov_base=&saved; ptrace(PTRACE_SETREGSET,pid,(void*)1,&iov);
    return result;
}

int main(int argc, char* argv[]) {
    if(argc<3){fprintf(stderr,"usage: %s <pid|pkg> <so_path>\n",argv[0]);return 1;}

    pid_t pid = (argv[1][0]>='0'&&argv[1][0]<='9')
        ? (pid_t)atoi(argv[1]) : find_pid(argv[1]);
    if(pid<=0){fprintf(stderr,"process not found: %s\n",argv[1]);return 1;}

    const char* so_path=argv[2];
    printf("[+] pid=%d  so=%s\n",pid,so_path);

    // --- Read .so into buffer ---
    int so_fd=open(so_path,O_RDONLY);
    if(so_fd<0){perror("open so");return 1;}
    struct stat st; fstat(so_fd,&st);
    size_t so_size=(size_t)st.st_size;
    void* so_buf=malloc(so_size);
    if(read(so_fd,so_buf,so_size)!=(ssize_t)so_size){perror("read so");return 1;}
    close(so_fd);
    printf("[+] so loaded: %zu bytes\n",so_size);

    // --- Resolve dlopen in target ---
    uintptr_t dlo_local=(uintptr_t)dlsym(RTLD_DEFAULT,"dlopen");
    if(!dlo_local) dlo_local=(uintptr_t)dlsym(RTLD_DEFAULT,"__loader_dlopen");

    // Find lib containing dlopen
    char dlo_lib[128]="libc.so";
    {
        FILE* f=fopen("/proc/self/maps","r"); char line[512];
        while(f&&fgets(line,sizeof(line),f)){
            uintptr_t s=(uintptr_t)strtoull(line,NULL,16);
            char* dash=strchr(line,'-');
            uintptr_t e=dash?(uintptr_t)strtoull(dash+1,NULL,16):0;
            if(dlo_local>=s&&dlo_local<e){
                char* p=strrchr(line,'/');
                if(p){snprintf(dlo_lib,sizeof(dlo_lib),"%s",p+1);
                    char* nl=strchr(dlo_lib,'\n');if(nl)*nl=0;}
                break;
            }
        }
        if(f)fclose(f);
    }
    uintptr_t local_base  = module_base(getpid(),dlo_lib);
    uintptr_t dlo_off     = dlo_local - local_base;
    uintptr_t remote_base = module_base(pid,dlo_lib);
    uintptr_t remote_dlo  = remote_base + dlo_off;
    printf("[+] dlopen in target: 0x%lx (%s)\n",(unsigned long)remote_dlo,dlo_lib);

    // --- Attach ---
    if(ptrace(PTRACE_ATTACH,pid,NULL,NULL)<0){perror("attach");return 1;}
    int status; waitpid(pid,&status,0);
    printf("[+] attached\n");

    // --- Find svc #0 in target's libc ---
    uintptr_t libc_base=module_base(pid,"libc.so");
    uintptr_t svc_addr=find_svc(pid,libc_base);
    if(!svc_addr){fprintf(stderr,"svc #0 not found\n");ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}

    // --- Allocate scratch memory in target (mmap RW) ---
    uintptr_t scratch = remote_syscall(pid,svc_addr,
        222/*mmap*/,0,0x2000,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,
        (uintptr_t)-1,0);
    if(scratch==(uintptr_t)-1||scratch==0){
        fprintf(stderr,"mmap in target failed\n");
        ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;
    }
    printf("[+] scratch: 0x%lx\n",(unsigned long)scratch);

    // --- Create memfd in target ---
    // memfd_create(name=scratch+0x100 (empty string), MFD_CLOEXEC=1) → returns fd
    // Write empty string at scratch+0x100
    uint8_t zero=0; mem_write(pid,scratch+0x100,&zero,1);
    uintptr_t memfd = remote_syscall(pid,svc_addr,
        279/*memfd_create*/,scratch+0x100,1/*MFD_CLOEXEC*/,0,0,0,0);
    if((int)memfd<0){fprintf(stderr,"memfd_create in target failed: %ld\n",(long)memfd);
        ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    printf("[+] target memfd: %lu\n",(unsigned long)memfd);

    // --- Write .so into target's memfd via /proc/<pid>/fd/<N> ---
    char fd_path[64]; snprintf(fd_path,sizeof(fd_path),"/proc/%d/fd/%lu",pid,(unsigned long)memfd);
    int wfd=open(fd_path,O_WRONLY);
    if(wfd<0){perror("open memfd path");ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    if(write(wfd,so_buf,so_size)!=(ssize_t)so_size){perror("write memfd");
        close(wfd);ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    close(wfd);
    free(so_buf);
    printf("[+] wrote %zu bytes to memfd\n",so_size);

    // --- Build dlopen path: "/proc/self/fd/<N>" ---
    char dlopen_path[64];
    snprintf(dlopen_path,sizeof(dlopen_path),"/proc/self/fd/%lu",(unsigned long)memfd);
    size_t path_len=strlen(dlopen_path)+1;

    // Write path to scratch+0
    mem_write(pid,scratch,dlopen_path,path_len);

    // --- Build shellcode at scratch+0x200 ---
    // ldr x0, [pc, #0x18]   ; x0 = pointer to path string
    // mov x1, #2             ; RTLD_NOW
    // ldr x16, [pc, #0x18]  ; x16 = dlopen addr
    // blr x16
    // brk #0
    // <8 bytes: path_ptr>
    // <8 bytes: dlopen_ptr>
    uintptr_t code_addr = scratch+0x200;
    uintptr_t path_ptr_slot   = code_addr+0x18;
    uintptr_t dlo_ptr_slot    = code_addr+0x20;

    uint8_t sc[0x28]={};
    {int64_t off=(int64_t)(path_ptr_slot-code_addr);
     uint32_t i=0x58000000|(((uint32_t)(off>>2)&0x7FFFF)<<5)|0;
     memcpy(sc,&i,4);}
    {uint32_t i=0xD2800041; memcpy(sc+4,&i,4);}
    {int64_t off=(int64_t)(dlo_ptr_slot-(code_addr+8));
     uint32_t i=0x58000000|(((uint32_t)(off>>2)&0x7FFFF)<<5)|16;
     memcpy(sc+8,&i,4);}
    {uint32_t i=0xD63F0200; memcpy(sc+12,&i,4);}
    {uint32_t i=0xD4200000; memcpy(sc+16,&i,4);} // brk #0
    // literal pool
    uintptr_t path_ptr_val=scratch; // path string is at scratch+0
    memcpy(sc+0x18,&path_ptr_val,8);
    memcpy(sc+0x20,&remote_dlo,8);
    mem_write(pid,code_addr,sc,sizeof(sc));

    // --- Make code page executable in target ---
    // mprotect(scratch, 0x2000, RWX)
    remote_syscall(pid,svc_addr,
        226/*mprotect*/,scratch,0x2000,PROT_READ|PROT_WRITE|PROT_EXEC,0,0,0);

    // --- Redirect PC to shellcode ---
    struct iovec iov;
    arm64_regs regs, saved;
    iov.iov_base=&regs; iov.iov_len=sizeof(regs);
    ptrace(PTRACE_GETREGSET,pid,(void*)1,&iov);
    memcpy(&saved,&regs,sizeof(regs));
    regs.pc=code_addr;
    iov.iov_base=&regs; ptrace(PTRACE_SETREGSET,pid,(void*)1,&iov);
    ptrace(PTRACE_CONT,pid,NULL,NULL);
    waitpid(pid,&status,0);

    if(WIFSTOPPED(status)&&WSTOPSIG(status)==SIGTRAP)
        printf("[+] shellcode done — library loaded via memfd\n");
    else
        fprintf(stderr,"[-] unexpected signal %d\n",WSTOPSIG(status));

    iov.iov_base=&saved; ptrace(PTRACE_SETREGSET,pid,(void*)1,&iov);

    // DETACH — TracerPid goes to 0 immediately
    ptrace(PTRACE_DETACH,pid,NULL,NULL);
    printf("[+] detached\n");

    // Unlink .so from disk — no trace on filesystem
    unlink(so_path);
    printf("[+] unlinked %s\n",so_path);
    printf("[+] DONE — /proc/%d/maps will show anonymous memfd\n",pid);
    return 0;
}

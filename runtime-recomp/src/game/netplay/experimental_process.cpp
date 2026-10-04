#include "experimental_process.hpp"
#include <atomic>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace dkr::runtime::netplay::experimental {
namespace {
std::atomic<bool> application_running=false;
#if defined(_WIN32)
std::wstring quote(std::wstring_view value) {
    std::wstring result=L"\"";unsigned slash=0;
    for(wchar_t c:value) {
        if(c==L'\\'){++slash;continue;}
        result.append(c==L'"'?slash*2+1:slash,L'\\');slash=0;result+=c;
    }
    result.append(slash*2,L'\\');return result+L'"';
}
struct Child {
    HANDLE job=nullptr,process=nullptr;bool done=false;
    Child()=default;Child(const Child&)=delete;Child& operator=(const Child&)=delete;
    ~Child(){if(job&&!done)TerminateJobObject(job,1);if(process){if(job&&!done)WaitForSingleObject(process,5000);CloseHandle(process);}if(job)CloseHandle(job);}
    bool start(const std::filesystem::path& exe,std::span<const std::string> args,bool bounded,std::string& error) {
        if(bounded) {
            job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {error="Cannot create bootstrap lifecycle job.";return false;}
        }
        auto command=quote(exe.native());for(const auto& arg:args)command+=L" "+quote(std::filesystem::u8path(arg).native());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&startup,&info)) {error="Cannot start experimental application child.";return false;}
        process=info.hProcess;
        if((job&&!AssignProcessToJobObject(job,process))||ResumeThread(info.hThread)==DWORD(-1)) {
            TerminateProcess(process,1);WaitForSingleObject(process,5000);CloseHandle(info.hThread);error="Cannot start isolated bootstrap child.";return false;
        }
        CloseHandle(info.hThread);return true;
    }
    int poll() {
        const auto wait=WaitForSingleObject(process,0);
        if(wait==WAIT_FAILED)return 1;
        if(wait!=WAIT_OBJECT_0)return -1;
        DWORD code=1;GetExitCodeProcess(process,&code);done=true;return code==0?0:1;
    }
};
#else
struct Child {
    pid_t pid=0;bool done=false,bounded=false;
    Child()=default;Child(const Child&)=delete;Child& operator=(const Child&)=delete;
    ~Child(){if(pid&&bounded&&!done){kill(-pid,SIGKILL);int status;while(waitpid(pid,&status,0)<0&&errno==EINTR){}}}
    bool start(const std::filesystem::path& exe,std::span<const std::string> args,bool limit,std::string& error) {
        bounded=limit;std::vector<std::string> storage{exe.native()};storage.insert(storage.end(),args.begin(),args.end());
        std::vector<char*> argv;for(auto& arg:storage)argv.push_back(arg.data());argv.push_back(nullptr);
        posix_spawnattr_t attributes;
        if(posix_spawnattr_init(&attributes)){error="Cannot initialise child process.";return false;}
        const int configured=posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP)|posix_spawnattr_setpgroup(&attributes,0);
        const int result=configured?configured:posix_spawn(&pid,exe.c_str(),nullptr,&attributes,argv.data(),environ);
        posix_spawnattr_destroy(&attributes);
        if(result){pid=0;error="Cannot start experimental child process.";return false;}return true;
    }
    int poll() {
        int status=0;const auto result=waitpid(pid,&status,WNOHANG);
        if(result==0||(result<0&&errno==EINTR))return -1;
        done=true;return result==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0?0:1;
    }
};
#endif
bool valid(const std::filesystem::path& exe,std::span<const std::string> args,std::string& error) {
    std::error_code filesystem_error;
    if(!exe.is_absolute()||!std::filesystem::is_regular_file(exe,filesystem_error)||args.size()>16){error="Missing application-supplied experimental executable.";return false;}
    for(const auto& arg:args)if(arg.size()>32768||arg.find('\0')!=std::string::npos){error="Invalid experimental child argument.";return false;}return true;
}
}
bool run_child(const std::filesystem::path& exe,std::span<const std::string> args,const std::function<bool()>& keep,
    std::chrono::seconds timeout,std::string& error) {
    if(!valid(exe,args,error)||timeout.count()<=0)return false;
    Child child;if(!child.start(exe,args,true,error))return false;
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(true) {
        const auto result=child.poll();if(result>=0){if(result)error="The local bootstrap did not complete; inspect its separate runtime log.";return result==0;}
        if(keep&&!keep()){error="Experimental preparation cancelled.";return false;}
        if(std::chrono::steady_clock::now()>=deadline){error="Local bootstrap preparation timed out.";return false;}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
bool launch_child(const std::filesystem::path& exe,std::span<const std::string> args,std::string& error) {
    if(!valid(exe,args,error))return false;
    bool expected=false;
    if(!application_running.compare_exchange_strong(expected,true)) {
        error="An experimental race test is already open. Close it before starting another.";return false;
    }
    Child child;if(!child.start(exe,args,false,error)){application_running=false;return false;}
#if defined(_WIN32)
    const auto process=child.process;
    try {std::thread([process]{WaitForSingleObject(process,INFINITE);CloseHandle(process);application_running=false;}).detach();}
    catch(...) {TerminateProcess(process,1);WaitForSingleObject(process,5000);application_running=false;error="Cannot track experimental application lifecycle.";return false;}
    child.process=nullptr;
#else
    const auto pid=child.pid;
    try {std::thread([pid]{int status;while(waitpid(pid,&status,0)<0&&errno==EINTR){};application_running=false;}).detach();}
    catch(...) {kill(-pid,SIGKILL);int status;while(waitpid(pid,&status,0)<0&&errno==EINTR){};application_running=false;error="Cannot reap experimental application child.";return false;}
#endif
    child.done=true;return true;
}
}

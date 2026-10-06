#include "online_mod_cache.hpp"
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#endif

namespace dkr::mods::online {
bool cached_payload(const std::filesystem::path& directory,const Payload& expected) {
    check_storage(directory,true);const auto file=private_storage_path(directory)/expected.digest;
    if(!std::filesystem::exists(file))return false;
    check_storage(file,false);
    if(std::filesystem::file_size(file)!=expected.size || sha256(read_file(file,static_cast<std::size_t>(expected.size)))!=expected.digest)
        throw Error("A cached online mod is corrupt. No profile was activated; remove the affected online cache before retrying.");
    return true;
}
void cache_bundle(const Bundle& bundle,const std::filesystem::path& directory,std::stop_token stop) {
    check_storage(directory,true);
    for(const auto& item:bundle.manifest.payloads) {
        if(stop.stop_requested())throw Error("Online mod preparation cancelled.");
        if(cached_payload(directory,item))continue;
        const auto found=bundle.payloads.find(item.digest);
        if(found==bundle.payloads.end() || !found->second || found->second->size()!=item.size || sha256(*found->second)!=item.digest)
            throw Error("Host mod bundle failed payload verification.");
        // New files only; another completed transaction may win this race.
        try{write_new_file(directory/item.digest,*found->second);}
        catch(...){if(!cached_payload(directory,item))throw;}
    }
}
PayloadFile::PayloadFile(const std::filesystem::path& partial,const Payload& expected)
    :path_(private_storage_path(partial)),expected_(expected) {
    if(!valid_digest(expected.digest) || !expected.size || expected.size>MaxTransferBytes)
        throw Error("Invalid online payload file budget.");
    check_storage(path_.parent_path(),true);
    if(std::filesystem::exists(path_))check_storage(path_,false);
    if(std::filesystem::space(path_.parent_path()).available<expected.size+1280ULL*MiB)
        throw Error("Not enough free space to download and safely prepare these online mods.");
#ifdef _WIN32
    const auto file=CreateFileW(path_.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw Error("Could not reserve the private online download.");
    handle_=reinterpret_cast<std::intptr_t>(file);
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
    if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
       !GetFileSizeEx(file,&length) || length.QuadPart<0 || static_cast<std::uint64_t>(length.QuadPart)>expected.size ||
       !SetFilePointerEx(file,length,nullptr,FILE_BEGIN)){close();throw Error("The partial online download is invalid or exceeds its declared size.");}
    size_=static_cast<std::uint64_t>(length.QuadPart);
#else
    const int file=open(path_.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(file<0)throw Error("Could not reserve the private online download.");handle_=file;
    struct stat info{};
    if(flock(file,LOCK_EX|LOCK_NB)!=0 || fstat(file,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0 || static_cast<std::uint64_t>(info.st_size)>expected.size ||
       lseek(file,0,SEEK_END)!=info.st_size){close();throw Error("The partial online download is invalid or exceeds its declared size.");}
    size_=static_cast<std::uint64_t>(info.st_size);
#endif
}
PayloadFile::~PayloadFile(){close();}
void PayloadFile::close()noexcept {
    if(handle_==-1)return;
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
    ::close(static_cast<int>(handle_));
#endif
    handle_=-1;
}
void PayloadFile::append(View bytes) {
    if(handle_==-1 || bytes.empty() || bytes.size()>8192 || bytes.size()>expected_.size-size_)
        throw Error("Online mod write is outside its declared payload.");
#ifdef _WIN32
    DWORD count=0;const auto file=reinterpret_cast<HANDLE>(handle_);
    if(!WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count!=bytes.size() || !FlushFileBuffers(file))
        throw Error("The online mod chunk could not be written durably.");
#else
    std::size_t at=0;const int file=static_cast<int>(handle_);
    while(at<bytes.size()) {
        const auto count=write(file,bytes.data()+at,bytes.size()-at);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)throw Error("The online mod chunk could not be written completely.");
        at+=static_cast<std::size_t>(count);
    }
    if(fsync(file)!=0)throw Error("The online mod chunk could not be written durably.");
#endif
    size_+=bytes.size();
}
void PayloadFile::publish(const std::filesystem::path& target) {
    if(size_!=expected_.size)throw Error("An incomplete online mod cannot be published.");
    close();check_storage(path_,false);
    if(sha256(read_file(path_,static_cast<std::size_t>(expected_.size)))!=expected_.digest)
        throw Error("The completed online download failed SHA-256 verification. No mod was activated.");
    check_storage(target.parent_path(),true);
    if(std::filesystem::exists(target)) {
        if(!cached_payload(target.parent_path(),expected_))throw Error("Online payload cache publication failed.");
        return; // Never replace a cache entry owned by a concurrent transaction.
    }
    // Publish the fully flushed file without replacing a target
    // that another process published first. Both paths are on the same volume.
#ifdef _WIN32
    // No REPLACE_EXISTING flag. Also works for portable installs on FAT/exFAT.
    if(!MoveFileExW(path_.c_str(),private_storage_path(target).c_str(),MOVEFILE_WRITE_THROUGH) &&
       !cached_payload(target.parent_path(),expected_))throw Error("Could not publish the verified online payload.");
#else
    std::error_code error;std::filesystem::create_hard_link(path_,target,error);
    if(error && !cached_payload(target.parent_path(),expected_))throw Error("Could not publish the verified online payload.");
#endif
}
}

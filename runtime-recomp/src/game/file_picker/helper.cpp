#include "protocol.hpp"
#include "native_arguments.hpp"
#include "updates/update_process.hpp"
#include <nfd.h>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/resource.h>
#endif

int main(int argc,char** argv) {
    using namespace dkr::file_picker;namespace fs=std::filesystem;
    auto values=native_arguments(argc,argv);std::vector<char*> arguments;for(auto& value:values)arguments.push_back(value.data());argc=arguments.size();argv=arguments.data();
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#else
    const rlimit no_core{0,0};if(setrlimit(RLIMIT_CORE,&no_core))return 2;
#endif
    if(argc==2 && std::string_view(argv[1])=="--self-test") {
        validate(Json{{"schema",1},{"kind","open"},{"initial",""},{"name",""},{"filters",Json::array()}});
        std::puts("DKR-R file picker protocol 1");return 0;
    }
    try {
        if(argc!=4 || std::string_view(argv[1])!="--request")return 2;
        const auto directory=fs::canonical(fs::u8path(argv[2]));dkr::updates::require_private_tree(directory);
        const auto path=directory/"request.json";
        if(fs::symlink_status(path).type()!=fs::file_type::regular || fs::file_size(path)>MaxMessage)return 2;
        std::ifstream in(path,std::ios::binary);std::string data{std::istreambuf_iterator<char>(in),{}};
        const auto request=Json::parse(data);validate(request);
        const auto parent=std::stoull(argv[3]);if(!dkr::updates::parent_alive(parent))return 2;
        std::jthread monitor([parent,directory](std::stop_token stop){while(!stop.stop_requested()){if(!dkr::updates::parent_alive(parent)){std::error_code e;for(const auto* name:{"request.json","result.json","result.json.new"})fs::remove(directory/name,e);fs::remove(directory,e);std::_Exit(2);}std::this_thread::sleep_for(std::chrono::milliseconds(100));}});
        Json result{{"schema",1},{"status","error"},{"path",""}};
        if(NFD_Init()==NFD_OKAY) {
            // JSON storage stays alive through the blocking native dialog.
            std::vector<nfdu8filteritem_t> filters;
            for(const auto& filter:request["filters"])filters.push_back({filter["name"].get_ref<const std::string&>().c_str(),filter["spec"].get_ref<const std::string&>().c_str()});
            const auto& initial=request["initial"].get_ref<const std::string&>();
            const auto& name=request["name"].get_ref<const std::string&>();
            nfdu8char_t* selected=nullptr;nfdresult_t status;
            if(request["kind"]=="folder")status=NFD_PickFolderU8(&selected,initial.empty()?nullptr:initial.c_str());
            else if(request["kind"]=="save")status=NFD_SaveDialogU8(&selected,filters.data(),filters.size(),initial.empty()?nullptr:initial.c_str(),name.empty()?nullptr:name.c_str());
            else status=NFD_OpenDialogU8(&selected,filters.data(),filters.size(),initial.empty()?nullptr:initial.c_str());
            result["status"]=status==NFD_OKAY?"okay":status==NFD_CANCEL?"cancel":"error";
            if(status==NFD_OKAY && selected)result["path"]=selected;
            if(selected)NFD_FreePathU8(selected);
            NFD_Quit();
        }
        dkr::updates::durable_write(directory/"result.json",result.dump());return 0;
    }catch(...){return 2;}
}

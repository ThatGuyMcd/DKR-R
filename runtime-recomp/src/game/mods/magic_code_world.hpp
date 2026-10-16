#pragma once
#include "magic_code_link.hpp"

namespace dkr::mods::code {
struct Effect {std::string package,text;};
// Exclusive session participant. Never reads mutable launcher configuration,
// uses the host clock, or patches process-wide function tables.
class World {
public:
    using Native=std::function<void(std::uint32_t,Registers&,Budget&)>;
    using Service=std::function<bool(std::string_view,std::size_t,Memory&,Registers&,Budget&)>;
    World(std::shared_ptr<const LinkedSet>,std::uint64_t seed,Native native={},Service service={});
    static LinkPolicy core_policy();
    void attach(std::span<std::uint8_t> ram,bool initial_install);
    void initialize(Budget&);
    void event(std::string_view owner,std::string_view name,const Registers& arguments,Budget&);
    void call(std::uint32_t function,Registers&,Budget&);
    bool has_hook(std::uint32_t function)const;
    bool enter(std::uint32_t function,Registers&,Budget&);
    void leave(std::uint32_t function,const Registers&,Budget&);
    std::vector<Effect> take_effects();
    Bytes checkpoint()const;
    struct Restore;
    std::shared_ptr<Restore> stage(View)const;
    void commit(std::shared_ptr<Restore>);
    const LinkedSet& linked()const{return *linked_;}
    bool idle()const{return active_==0;}
private:
    struct Block {std::uint32_t offset=0,size=0;bool used=false;};
    struct State {std::uint64_t random=1;std::uint32_t fcsr=0;std::vector<Block> blocks;};
    std::shared_ptr<const LinkedSet> linked_;
    std::vector<State> state_;
    std::vector<unsigned> calls_;
    std::span<std::uint8_t> ram_;
    std::unique_ptr<Memory> bus_;
    Native native_;
    Service service_;
    std::vector<Effect> effects_;
    unsigned active_=0;
    bool initialized_=false;
    void gateway(std::uint32_t,Registers&,Budget&);
    void core(std::string_view,std::size_t,Memory&,Registers&,Budget&);
    Memory& memory()const;
    void verify_immutable()const;
};
}

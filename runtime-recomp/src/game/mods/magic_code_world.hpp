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
    using ArgumentStack=std::array<std::uint32_t,16>;
    World(std::shared_ptr<const LinkedSet>,std::uint64_t seed,Native native={},Service service={});
    static LinkPolicy core_policy();
    void attach(std::span<std::uint8_t> ram,bool initial_install);
    void detach();
    void initialize(Budget&);
    void event(std::string_view owner,std::string_view name,const Registers& arguments,Budget&,const ArgumentStack* native_arguments=nullptr);
    void call(std::uint32_t function,Registers&,Budget&,const ArgumentStack* native_arguments=nullptr);
    bool has_hook(std::uint32_t function)const;
    bool enter(std::uint32_t function,Registers&,Budget&,const ArgumentStack* native_arguments=nullptr);
    void leave(std::uint32_t function,const Registers&,Budget&,const ArgumentStack* native_arguments=nullptr);
    std::vector<Effect> take_effects();
    Bytes checkpoint()const;
    struct Restore;
    std::shared_ptr<Restore> stage(View)const;
    void commit(std::shared_ptr<Restore>);
    const LinkedSet& linked()const{return *linked_;}
    struct Metric {std::uint64_t callbacks=0,instructions=0,native_calls=0,nanoseconds=0;};
    const std::vector<Metric>& local_metrics()const{return metrics_;}
    struct Rebind {std::shared_ptr<const LinkedSet> linked;std::unique_ptr<Memory> bus;int migration=-1;};
    std::shared_ptr<const LinkedSet> image()const{return linked_;}
    std::unique_ptr<Rebind> prepare_rebind(std::shared_ptr<const LinkedSet>,int migration=-1)const;
    void exchange(Rebind&)noexcept;
    void owner_event(unsigned,std::string_view,Budget&,const Registers& args={});
    bool idle()const{return active_==0;}
private:
    struct Block {std::uint32_t offset=0,size=0;bool used=false;};
    struct State {std::uint64_t random=1;std::uint32_t fcsr=0;std::vector<Block> blocks;};
    std::shared_ptr<const LinkedSet> linked_;
    std::vector<State> state_;
    std::vector<unsigned> calls_;
    std::vector<Metric> metrics_;
    std::span<std::uint8_t> ram_;
    std::unique_ptr<Memory> bus_;
    Native native_;
    Service service_;
    std::vector<Effect> effects_;
    unsigned active_=0;
    int migration_owner_=-1;
    bool initialized_=false;
    void gateway(std::uint32_t,Registers&,Budget&);
    void core(std::string_view,std::size_t,Memory&,Registers&,Budget&);
    Memory& memory()const;
    void verify_immutable()const;
};
}

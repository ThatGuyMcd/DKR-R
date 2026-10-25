#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::achievements {
enum class Kind { Balloons,Key,Amulet,Trophy,Boss,Silver,TimeTrial,Developer,Power,Driver };
struct Definition {std::string id,title,description,category;Kind kind;int argument=0,target=1,vehicle=-1;bool mirrored=false,restorable=false;};
struct Course {int id,vehicle;const char* name;int tt_ticks,developer_centiseconds;};
inline constexpr std::array<Course,20> courses{{
 {5,0,"Ancient Lake",3677,5343},{3,0,"Fossil Canyon",5411,8155},
 {29,0,"Jungle Falls",3750,5413},{7,2,"Hot Top Volcano",5443,8248},
 {13,2,"Everfrost Peak",6480,9763},{6,0,"Walrus Cove",7577,11660},
 {9,0,"Snowball Valley",3985,5781},{28,0,"Frosty Village",5964,8801},
 {8,1,"Whale Bay",4387,6411},{10,0,"Crescent Island",5590,8501},
 {4,1,"Pirate Lagoon",5314,7351},{30,0,"Treasure Caves",3938,5505},
 {20,2,"Windmill Plains",7988,12316},{18,0,"Greenwood Village",6037,9180},
 {19,1,"Boulder Canyon",7341,11115},{31,0,"Haunted Woods",4171,5825},
 {17,2,"Spacedust Alley",7570,12038},{32,0,"Darkmoon Caverns",7923,12568},
 {15,2,"Spaceport Alpha",7742,11500},{33,0,"Star City",7287,11296}
}};
// Native 60-Hz integer ghost times, checked against the original ROM headers,
// not rounded screen timestamps or a possibly modified/stale live ghost.
struct Racer {std::uint32_t address=0;int player=-1,character=-1,vehicle=-1,bananas=0,power=-1,power_level=0,power_count=0,coins=0,finished=0,position=0,lap=0,total_ticks=0;};
struct Sample {
 bool valid=false,custom=false,tracks=false,mirrored=false,trial=false,paused=false;
 int mode=-1,menu=-1,level=-1,slot=-1,balloons=0,keys=0,tt_amulet=0,wizpig_amulet=0,trophies=0,bosses=0,laps=0,race_type=-1,tt_ticks=0,trophy_world=0,trophy_round=0;
 std::uint32_t settings=0;std::array<std::uint32_t,34> course_flags{};std::array<Racer,4> racers{};
};
struct Entry {bool unlocked=false,restored=false,modded=false;std::int64_t when=0;int progress=0;};
struct Preferences {bool notifications=true,sound=true,reduced_motion=false;int position=0;float scale=1,duration=5;};
struct View {std::array<Entry,94> entries{};Preferences preferences;std::string pinned,error,status="Saved on this device";unsigned unlocked=0;};
struct Toast {std::string id,title;bool preview=false;};
const std::vector<Definition>& catalogue();
Sample capture(const std::uint8_t* ram,std::size_t bytes,unsigned revision) noexcept;
void configure(const std::filesystem::path& root);
void begin(bool online,unsigned local_owner=0,bool modded=false,bool development=false) noexcept;
void submit(const Sample&,std::uint64_t epoch=0,std::uint32_t frame=0) noexcept;
std::shared_ptr<const View> view();
void preferences(Preferences);
void pin(std::string id);
void restore_progress();
void reset_collection();
void export_progress(const std::filesystem::path&);
void import_progress(const std::filesystem::path&);
void preview();
bool next_toast(Toast&);
bool notification_active();
bool online_session() noexcept;
}

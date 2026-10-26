#include "achievements.hpp"
#include "shared_snapshot.hpp"
#include "achievement_symbols.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace dkr::runtime::achievements {
namespace {
using Json=nlohmann::json;
std::atomic<bool> online{false};std::atomic<unsigned> owner{0};std::atomic<bool> modified{false};
std::atomic<bool> development{false};
class Memory {
 const std::uint8_t* ram_;std::size_t size_;
public:
 Memory(const std::uint8_t* ram,std::size_t size):ram_(ram),size_(size){}
 bool range(std::uint32_t p,unsigned n)const{return ram_&&p>=0x80000000U&&p<0xa0000000U&&(p&0x1fffffffU)<=size_&&n<=size_-(p&0x1fffffffU);}
 unsigned get(std::uint32_t p,unsigned n=4)const {if(!range(p,n))return 0;unsigned v=0;for(unsigned i=0;i<n;++i)v=v<<8|ram_[((p&0x1fffffffU)+i)^3U];return v;}
};
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
Json read(const std::filesystem::path& path) {
 std::error_code ec;if(!std::filesystem::exists(path,ec))return Json::object();
 if(std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>512*1024)throw std::runtime_error("Achievement file is unsafe or too large.");
 std::ifstream in(path,std::ios::binary);auto doc=Json::parse(in);
 if(!doc.is_object()||doc.value("schema",0)!=1||!doc.contains("entries")||!doc["entries"].is_object()||doc["entries"].size()>2048)throw std::runtime_error("Achievement file has an unsupported format.");
 for(const auto& [id,e]:doc["entries"].items()) {
  if(id.empty()||id.size()>100||!e.is_object())throw std::runtime_error("Invalid achievement entry.");
  (void)e.value("unlocked",false);(void)e.value("restored",false);(void)e.value("modded",false);
  auto time=e.value("when",std::int64_t(0));auto p=e.value("progress",0);
  if(time<0||p<0||p>1000000)throw std::runtime_error("Invalid achievement progress.");
 }
 return doc;
}
class FileLease {
#ifdef _WIN32
 HANDLE value_=INVALID_HANDLE_VALUE;
#else
 int value_=-1;
#endif
public:
 explicit FileLease(const std::filesystem::path& path) {
  if(std::filesystem::is_symlink(path))throw std::runtime_error("Achievement storage cannot use symbolic links.");
#ifdef _WIN32
  for(unsigned n=0;n<100&&value_==INVALID_HANDLE_VALUE;++n){value_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(value_==INVALID_HANDLE_VALUE)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  if(value_==INVALID_HANDLE_VALUE)throw std::runtime_error("Another DKR-R copy is updating achievements. Please try again.");
#else
  value_=::open(path.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW,0600);
  if(value_<0)throw std::runtime_error("Cannot open the achievement lock.");
  if(flock(value_,LOCK_EX)!=0){::close(value_);value_=-1;throw std::runtime_error("Cannot lock achievement progress.");}
#endif
 }
 ~FileLease(){
#ifdef _WIN32
  if(value_!=INVALID_HANDLE_VALUE)CloseHandle(value_);
#else
  if(value_>=0){flock(value_,LOCK_UN);::close(value_);}
#endif
 }
};
void atomic_write(const std::filesystem::path& path,const Json& doc,bool backup=true) {
 std::filesystem::create_directories(path.parent_path());
 if(std::filesystem::is_symlink(path)||std::filesystem::is_symlink(path.parent_path()))throw std::runtime_error("Achievement storage cannot use symbolic links.");
 auto pending=path;pending+=".pending";
 auto previous=path;previous+=".backup";
 if(std::filesystem::is_symlink(pending)||std::filesystem::is_symlink(previous))throw std::runtime_error("Achievement storage cannot use symbolic links.");
 const auto text=doc.dump(2)+"\n";
 {std::ofstream out(pending,std::ios::binary|std::ios::trunc);out.write(text.data(),std::streamsize(text.size()));out.flush();if(!out)throw std::runtime_error("Cannot write achievement progress.");}
 if(backup&&std::filesystem::exists(path)) {
  // Never turn a corrupt primary into the last-known-good backup.
  (void)read(path);
  std::filesystem::copy_file(path,previous,std::filesystem::copy_options::overwrite_existing);
 }
#ifdef _WIN32
 if(!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit achievement progress.");
#else
 const int file=::open(pending.c_str(),O_RDONLY|O_NOFOLLOW);if(file<0)throw std::runtime_error("Cannot flush achievement progress.");
 const auto flushed=::fsync(file);::close(file);if(flushed)throw std::runtime_error("Cannot flush achievement progress.");
 std::filesystem::rename(pending,path);const int directory=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(directory>=0){::fsync(directory);::close(directory);}
#endif
}
struct Event {Sample sample;bool reset=false,is_online=false,modded=false;unsigned local=0;std::uint64_t epoch=0;std::uint32_t frame=0;};
class Service {
 std::mutex mutex_;std::condition_variable cv_;bool stopping_=false;
 std::deque<std::function<void()>> jobs_;std::deque<Toast> toasts_;
 dkr::runtime::SharedSnapshot<const View> published_{std::make_shared<View>()};
 std::thread worker_;std::filesystem::path path_;View state_;Sample previous_,latest_;
 bool baseline_=false;Racer adventure_human_;std::array<int,10> driver_{};std::array<std::set<int>,10> earned_balloon_totals_;
 std::uint64_t last_epoch_=~0ULL;std::uint32_t last_frame_=~0U;Json retained_=Json::object();
 void publish(){state_.unlocked=unsigned(std::count_if(state_.entries.begin(),state_.entries.end(),[](auto& e){return e.unlocked;}));published_.store(std::make_shared<const View>(state_));}
 void merge(const Json& doc) {
  if(doc.empty())return;
  for(unsigned i=0;i<catalogue().size();++i){auto f=doc["entries"].find(catalogue()[i].id);if(f==doc["entries"].end())continue;auto& e=state_.entries[i];
   e.progress=std::max(e.progress,std::clamp(f->value("progress",0),0,catalogue()[i].target));
   if(f->value("unlocked",false)&&!e.unlocked)e={true,f->value("restored",false),f->value("modded",false),f->value("when",std::int64_t(0)),e.progress};
  }
 }
 Json document(){auto doc=retained_;doc["schema"]=1;doc["catalogue"]="dkr-r-94-v1";doc["entries"]=doc.value("entries",Json::object());
  for(unsigned i=0;i<catalogue().size();++i){auto& e=state_.entries[i];doc["entries"][catalogue()[i].id]={{"unlocked",e.unlocked},{"restored",e.restored},{"modded",e.modded},{"when",e.when},{"progress",e.progress}};}
  const auto& p=state_.preferences;doc["preferences"]={{"notifications",p.notifications},{"sound",p.sound},{"reduced_motion",p.reduced_motion},{"position",p.position},{"scale",p.scale},{"duration",p.duration}};doc["pinned"]=state_.pinned;return doc;
 }
 void persist(bool union_existing=true){if(path_.empty())return;FileLease lease(path_.parent_path()/"progress.lock");if(union_existing){const auto existing=read(path_);merge(existing);if(!existing.empty())retained_=existing;}atomic_write(path_,document());state_.error.clear();}
 void award(unsigned i,bool restored,bool modded){auto& e=state_.entries[i];if(e.unlocked)return;e.unlocked=true;e.restored=restored;e.modded=modded;e.when=now();e.progress=catalogue()[i].target;
  std::fprintf(stderr,"[achievements][unlock] id=%s restored=%u modded=%u\n",catalogue()[i].id.c_str(),unsigned(restored),unsigned(modded));
  if(state_.pinned==catalogue()[i].id)state_.pinned.clear();
  if(!restored){std::lock_guard lock(mutex_);toasts_.push_back({catalogue()[i].id,catalogue()[i].title,false});}
 }
 void evaluate(const Event& event) {
  if(event.reset){baseline_=false;adventure_human_={};driver_.fill(0);for(auto& values:earned_balloon_totals_)values.clear();last_epoch_=~0ULL;last_frame_=~0U;}
  const auto& s=event.sample;if(!s.valid){baseline_=false;return;}latest_=s;
  if(event.is_online){if(event.epoch==last_epoch_&&last_frame_!=~0U&&event.frame<=last_frame_)return;last_epoch_=event.epoch;last_frame_=event.frame;}
  const bool same=baseline_&&s.settings==previous_.settings&&s.slot==previous_.slot&&s.mirrored==previous_.mirrored;
  const bool same_race=same&&s.level==previous_.level&&s.tracks==previous_.tracks&&s.trial==previous_.trial&&s.custom==previous_.custom&&previous_.mode==0;
  const bool adventure=!s.tracks&&!s.custom&&s.slot>=0&&s.slot<3;
  if(!same||s.tracks||(s.mode==1&&(s.menu==0||s.menu==3||s.menu==6||s.menu==19))){adventure_human_={};driver_.fill(0);for(auto& v:earned_balloon_totals_)v.clear();}
  const Racer* human=nullptr;for(const auto& r:s.racers)if(r.address&&r.player>=0&&r.player<4&&(!event.is_online||unsigned(r.player)==event.local)){human=&r;break;}
  if(!human&&same)for(const auto& r:previous_.racers)if(r.address&&r.player>=0&&r.player<4&&(!event.is_online||unsigned(r.player)==event.local)){human=&r;break;}
  if(human&&adventure)adventure_human_=*human;
  if(!human&&adventure&&adventure_human_.address)human=&adventure_human_;
  const int delta=same?s.balloons-previous_.balloons:0;
  const bool balloon=adventure&&same&&human&&delta>0&&delta<=2&&previous_.mode==0;
  if(balloon&&human->character>=0&&human->character<10){auto& seen=earned_balloon_totals_[human->character];for(int n=previous_.balloons+1;n<=s.balloons;++n)if(seen.insert(n).second)++driver_[human->character];}
  bool changed=false;
  for(unsigned i=0;i<catalogue().size();++i){const auto& d=catalogue()[i];auto& e=state_.entries[i];if(e.unlocked)continue;bool earned=false;int progress=e.progress;
   switch(d.kind){
    case Kind::Balloons:if(adventure&&(!d.mirrored||s.mirrored)){progress=std::max(progress,std::min(s.balloons,d.target));earned=balloon&&s.balloons>=d.target&&previous_.balloons<d.target;}break;
    case Kind::Key:{const auto mask=1<<(d.argument+1);earned=adventure&&same&&human&&(s.keys&mask)&&!(previous_.keys&mask)&&previous_.mode==0;break;}
    case Kind::Amulet:{const auto value=d.argument?s.wizpig_amulet:s.tt_amulet,old=d.argument?previous_.wizpig_amulet:previous_.tt_amulet;if(adventure)progress=std::max(progress,std::min(value,4));earned=adventure&&same&&human&&value==4&&old<4&&previous_.mode==0;break;}
    case Kind::Trophy:{const int value=(s.trophies>>(d.argument*2))&3,old=(previous_.trophies>>(d.argument*2))&3;earned=adventure&&same&&human&&value==3&&old<3;break;}
    case Kind::Boss:earned=adventure&&same&&human&&(!d.mirrored||s.mirrored)&&(s.bosses&d.argument)&&!(previous_.bosses&d.argument)&&previous_.mode==0;break;
    case Kind::Driver:if(adventure){progress=driver_[d.argument];earned=balloon&&progress>=5;}break;
    case Kind::Silver:
    case Kind::TimeTrial:
    case Kind::Developer:
    case Kind::Power:
     for(unsigned p=0;p<s.racers.size();++p){const auto& r=s.racers[p];const auto& old=previous_.racers[p];
      if(!r.address||r.player<0||r.player>3||(event.is_online&&unsigned(r.player)!=event.local)||s.mode!=0||s.paused)continue;
      const bool finish=same_race&&r.address==old.address&&r.finished&&!old.finished&&r.total_ticks>0&&s.race_type==0;
      if(d.kind==Kind::Power){if(s.race_type!=0&&s.race_type<64)continue;if(d.argument==5){progress=std::max(progress,std::min(r.bananas,10));earned|=r.bananas>=10&&same_race&&r.address==old.address&&old.bananas<10;}
       else earned|=r.power==d.argument&&r.power_level>=2&&r.power_count>0&&same_race&&r.address==old.address&&(old.power!=r.power||old.power_level<2||old.power_count==0);continue;}
      if(s.custom||s.level!=d.argument)continue;
      if(d.kind==Kind::Silver){earned|=finish&&adventure&&s.mirrored&&!s.trial&&r.coins==8&&r.position==1;continue;}
      if(!s.trial||s.mirrored||s.laps!=3||r.vehicle!=d.vehicle||!finish)continue;
      if(d.kind==Kind::TimeTrial)earned|=r.total_ticks<d.target;
      else earned|=std::int64_t(r.total_ticks)*100<std::int64_t(d.target)*60;
     }break;
   }
   progress=std::clamp(progress,0,d.target);if(progress!=e.progress){e.progress=progress;changed=true;}if(earned){award(i,false,event.modded);changed=true;}
  }
  previous_=s;baseline_=true;
  if(changed){persist();publish();}
 }
public:
 Service():worker_([this]{for(;;){std::function<void()> job;{std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stopping_||!jobs_.empty();});if(jobs_.empty()&&stopping_)return;job=std::move(jobs_.front());jobs_.pop_front();}try{job();}catch(const std::exception& e){state_.error=e.what();state_.status="Achievement progress needs attention";std::fprintf(stderr,"[achievements][storage] %s\n",e.what());publish();}}}){}
 ~Service(){{std::lock_guard lock(mutex_);stopping_=true;}cv_.notify_one();worker_.join();}
 void post(std::function<void()> job){std::lock_guard lock(mutex_);if(jobs_.size()>=8192)throw std::runtime_error("Achievement event queue is full; storage is not keeping up.");jobs_.push_back(std::move(job));cv_.notify_one();}
 std::shared_ptr<const View> snapshot(){return published_.load();}
 void configure(const std::filesystem::path& root){post([this,root]{if(!path_.empty())return;path_=root/"achievements"/"progress-v1.json";std::filesystem::create_directories(path_.parent_path());FileLease lease(path_.parent_path()/"progress.lock");
  Json doc;try{doc=read(path_);}catch(...){auto backup=path_;backup+=".backup";doc=read(backup);if(doc.empty())throw;auto corrupt=path_;corrupt+=".corrupt-"+std::to_string(now());std::filesystem::copy_file(path_,corrupt);atomic_write(path_,doc,false);state_.status="Recovered achievement progress from backup";}
  if(!doc.empty()){retained_=doc;merge(doc);state_.pinned=doc.value("pinned",std::string{});if(doc.contains("preferences")){const auto& j=doc["preferences"];auto& p=state_.preferences;p.notifications=j.value("notifications",true);p.sound=j.value("sound",true);p.reduced_motion=j.value("reduced_motion",false);p.position=std::clamp(j.value("position",0),0,2);p.scale=std::clamp(j.value("scale",1.F),.7F,1.6F);p.duration=std::clamp(j.value("duration",5.F),2.F,10.F);}}
  publish();std::fprintf(stderr,"[achievements] collection=94 local-only v1; native-event observer ready\n");});}
 void sample(Event event){post([this,event]{evaluate(event);});}
 void mode(bool developer){post([this,developer]{baseline_=false;state_.status=developer?"Tracking paused for developer tools; your collection is safe":"Saved on this device";publish();});}
 void settings(Preferences p){post([this,p]{state_.preferences=p;state_.preferences.position=std::clamp(p.position,0,2);state_.preferences.duration=std::clamp(p.duration,2.F,10.F);state_.preferences.scale=std::clamp(p.scale,.7F,1.6F);persist();publish();});}
 void pinned(std::string id){post([this,id=std::move(id)]{state_.pinned=id;persist();publish();});}
 void restore(){post([this]{const auto& s=latest_;if(!s.valid||s.tracks||s.custom)throw std::runtime_error("Open a normal adventure before recognising its saved progress.");
  for(unsigned i=0;i<catalogue().size();++i){const auto& d=catalogue()[i];if(!d.restorable||(d.mirrored&&!s.mirrored))continue;bool yes=false;
   switch(d.kind){case Kind::Balloons:yes=s.balloons>=d.target;break;case Kind::Key:yes=s.keys&(1<<(d.argument+1));break;case Kind::Amulet:yes=(d.argument?s.wizpig_amulet:s.tt_amulet)==4;break;case Kind::Trophy:yes=((s.trophies>>(2*d.argument))&3)==3;break;case Kind::Boss:yes=s.bosses&d.argument;break;case Kind::Silver:yes=s.mirrored&&d.argument<34&&(s.course_flags[d.argument]&4);break;default:break;}if(yes)award(i,true,true);
  }persist();state_.status="Recognised provable adventure progress; race challenges still need gameplay";publish();});}
 void reset(){post([this]{state_.entries={};state_.pinned.clear();baseline_=false;driver_.fill(0);persist(false);{std::lock_guard lock(mutex_);toasts_.clear();}publish();});}
 void export_to(std::filesystem::path path){post([this,path=std::move(path)]{atomic_write(path,document());state_.status="Achievement backup exported";publish();});}
 void import_from(std::filesystem::path path){post([this,path=std::move(path)]{const auto doc=read(path);if(doc.empty())throw std::runtime_error("The achievement backup is empty.");merge(doc);persist();baseline_=false;state_.status="Achievement backup merged; no game saves were changed";publish();});}
 void toast_preview(){std::lock_guard lock(mutex_);toasts_.push_back({"preview","Dino Domain Champion",true});}
 bool pop(Toast& toast){std::lock_guard lock(mutex_);if(toasts_.empty())return false;toast=std::move(toasts_.front());toasts_.pop_front();return true;}
 bool pending(){std::lock_guard lock(mutex_);return !toasts_.empty();}
};
Service& service(){static Service value;return value;}
std::atomic<bool> restart{true};
}
Sample capture(const std::uint8_t* ram,std::size_t bytes,unsigned revision) noexcept {
 Sample s;if(revision!=77&&revision!=80)return s;
 const auto& a=revision==80?symbols80:symbols77;Memory g(ram,bytes);s.settings=g.get(a.settings);
 if(!g.range(s.settings,0x118))return s;
 s.valid=true;s.mode=int(g.get(a.mode));s.menu=int(g.get(a.menu));s.tracks=g.get(a.tracks)!=0;s.mirrored=g.get(a.adventure_two)!=0;s.paused=g.get(a.paused,1)!=0;
 s.slot=int(g.get(a.slot));s.level=int(g.get(a.map));s.trial=g.get(a.trial,1)!=0;s.balloons=int(std::int16_t(g.get(g.get(s.settings),2)));
 if(s.balloons<0||s.balloons>47){s.valid=false;return s;}
 s.keys=int(g.get(s.settings+8,2));s.bosses=int(g.get(s.settings+12,2));s.trophies=int(g.get(s.settings+14,2));s.tt_amulet=int(g.get(s.settings+22,1));s.wizpig_amulet=int(g.get(s.settings+23,1));
 const auto flags=g.get(s.settings+4);if(g.range(flags,34*4))for(unsigned i=0;i<34;++i)s.course_flags[i]=g.get(flags+i*4);
 s.trophy_world=int(g.get(a.trophy_world));s.trophy_round=int(g.get(a.trophy_round));
 const auto header=g.get(a.header);if(g.range(header,0x50)){s.laps=int(g.get(header+0x4b,1));s.race_type=int(g.get(header+0x4c,1));}
 // Staff headers were checked against the original ROM by inspect_rom.py.
 // Use the catalogue's exact native ticks: a stale ghost or a modded ghost
 // must not silently move the goalposts for an original-course challenge.
 const auto racers=g.get(a.racers);if(s.mode!=0||!g.range(racers,16))return s;
 for(unsigned p=0;p<4;++p){const auto object=g.get(racers+p*4);if(!g.range(object,0x68)||g.get(object+0x48,2)!=1)continue;
  const auto data=g.get(object+0x64);if(!g.range(data,0x224))continue;auto& r=s.racers[p];r.address=data;r.player=std::int16_t(g.get(data,2));if(r.player<0||r.player>3){r.address=0;continue;}
  r.character=int(g.get(data+3,1));r.vehicle=int(g.get(data+0x1d7,1));r.bananas=int(g.get(data+0x185,1));r.power=int(g.get(data+0x172,1));r.power_count=int(g.get(data+0x173,1));r.power_level=int(g.get(data+0x174,1));r.coins=int(g.get(data+0x202,1));r.lap=int(g.get(data+0x193,1));r.finished=int(g.get(data+0x1d8,1));r.position=int(g.get(data+0x1ac,2));
  for(int lap=0;lap<std::clamp(s.laps,0,5);++lap){const auto time=g.get(data+0x128+lap*4);if(time>36000){r.total_ticks=0;break;}r.total_ticks+=int(time);}
 }
 return s;
}
void configure(const std::filesystem::path& root){service().configure(root);}
void begin(bool is_online,unsigned local,bool modded,bool developer)noexcept{online=is_online;owner=local;modified=modded;development=developer;restart=true;try{service().mode(developer);}catch(...) {}}
bool online_session()noexcept{return online.load();}
void submit(const Sample& sample,std::uint64_t epoch,std::uint32_t frame)noexcept{if(development)return;try{service().sample({sample,restart.exchange(false),online.load(),modified.load(),owner.load(),epoch,frame});}catch(...){std::fprintf(stderr,"[achievements] Event could not be queued; game state was not changed\n");}}
std::shared_ptr<const View> view(){return service().snapshot();}
void preferences(Preferences p){service().settings(p);}void pin(std::string id){service().pinned(std::move(id));}
void restore_progress(){service().restore();}void reset_collection(){service().reset();}
void export_progress(const std::filesystem::path& path){service().export_to(path);}void import_progress(const std::filesystem::path& path){service().import_from(path);}
void preview(){service().toast_preview();}bool next_toast(Toast& toast){return service().pop(toast);}bool notification_active(){return service().pending();}
}

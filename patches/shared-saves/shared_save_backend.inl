// Included at global scope by the staged save_manager.cpp. Existing codec,
// path guards, leases and durable replacement stay authoritative.
namespace dkr::runtime::saves::shared {
namespace {
using Json=nlohmann::json;
std::mutex operation_mutex;
std::filesystem::path data_root() {
    std::scoped_lock lock(g_save_manager_mutex);return g_config_directory;
}
bool read_json(const std::filesystem::path& path,Json& doc) {
    std::vector<std::uint8_t> bytes;
    if(!ReadFileBounded(path,16*1024*1024,bytes))return false;
    try {doc=Json::parse(bytes.begin(),bytes.end());return doc.is_object();}catch(...){return false;}
}
bool write_json(const std::filesystem::path& path,const Json& doc,std::string& error) {
    const auto text=doc.dump(2);return storage::write_atomic(path,{text.begin(),text.end()},nullptr,error);
}
std::filesystem::path transaction_destination(const Json& item) {
    const auto kind=item.at("kind").get<unsigned>();
    if(kind==kBundleAdventureKind&&item.at("channel")==0)return data_root()/"saves/dkr.us.v77.bin";
    if(kind==kBundleRecordsKind&&item.at("channel")==0)return data_root()/"saves/custom-records-v1.bin";
    const auto channel=item.at("channel").get<unsigned>();
    if(kind==kBundlePakKind&&channel<4)return data_root()/("controller-pak-"+std::to_string(channel+1)+".mpk");
    throw std::runtime_error("The save transaction contains an unsupported destination.");
}
bool retire_transaction(const std::filesystem::path& dir,std::string& error) {
    auto archive=data_root()/"save-transactions"/Timestamp();
    std::error_code ec;std::filesystem::create_directories(archive.parent_path(),ec);
    if(!ec&&std::filesystem::exists(archive,ec))archive+=std::to_string(++storage::serial);
    if(!storage::writable_path(archive/".check",error))return false;
    if(!ec)std::filesystem::rename(dir,archive,ec);
    if(ec){error="Save images are safe, but the transaction archive could not be finalised: "+ec.message();return false;}
    return true;
}
bool recover_unlocked(std::string& error) {
    const auto dir=data_root()/"pending-save-transaction";
    std::error_code ec;if(!std::filesystem::exists(dir,ec))return !ec;
    Json journal;
    if(!read_json(dir/"journal.json",journal)||journal.value("schema",0)!=1||
       !journal.contains("items")||!journal["items"].is_array()||journal["items"].size()>6) {
        error="An interrupted save transfer needs recovery. Its files were retained; do not delete pending-save-transaction.";return false;
    }
    if(journal.value("state","")=="committed")return retire_transaction(dir,error);
    std::vector<std::shared_ptr<storage::Lease>> owners;
    std::set<std::filesystem::path> seen;
    try {
        // Validate every original before rolling back even one destination.
        for(std::size_t i=0;i<journal["items"].size();++i) {
            const auto& item=journal["items"][i];const auto dest=transaction_destination(item);
            if(!seen.insert(dest).second)throw std::runtime_error("Duplicate save transfer destination.");
            auto owner=storage::acquire(dest,error);if(!owner)return false;owners.push_back(std::move(owner));
            if(item.at("existed").get<bool>()) {
                std::vector<std::uint8_t> bytes;
                const auto size=item.at("bytes").get<std::size_t>();
                if(size>1024*1024||!storage::read_exact(dir/(std::to_string(i)+".original"),size,bytes)||
                    storage::content_id(bytes)!=item.at("hash").get<std::string>())
                    throw std::runtime_error("An original save transfer image is missing or damaged.");
            }
        }
        for(std::size_t i=0;i<journal["items"].size();++i) {
            const auto& item=journal["items"][i];const auto dest=transaction_destination(item);
            if(item.at("existed").get<bool>()) {
                std::vector<std::uint8_t> bytes;
                if(!storage::read_exact(dir/(std::to_string(i)+".original"),item.at("bytes").get<std::size_t>(),bytes)||
                   !storage::write_atomic(dest,bytes,nullptr,error))return false;
            } else {
                // An absent-before file is preserved in the recovery archive,
                // never deleted without an original. Fixed typed paths only.
                if(!storage::writable_path(dest,error))return false;
                if(std::filesystem::exists(dest,ec)) {
                    const auto retained=dir/(std::to_string(i)+".uncommitted");
                    if(std::filesystem::exists(retained,ec)){error="A save recovery image already exists; originals were retained.";return false;}
                    std::filesystem::rename(dest,retained,ec);if(ec){error=ec.message();return false;}
                }
            }
        }
        journal["state"]="rolled-back";
        if(!write_json(dir/"journal.json",journal,error))return false;
        return retire_transaction(dir,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
}

std::string revision() {
    std::vector<std::uint8_t> bytes;
    return storage::read_exact(adventure_info().path,512,bytes)?storage::content_id(bytes):"missing";
}
bool recover_transaction(std::string& error) {
    std::scoped_lock lock(operation_mutex);return recover_unlocked(error);
}
bool prepare(const std::filesystem::path& root,std::string& error) {
    std::scoped_lock lock(operation_mutex);
    if(storage::absolute(root)!=storage::absolute(data_root())){error="Save preparation has the wrong data owner.";return false;}
    auto personal_owner=storage::acquire(root/"saves/dkr.us.v77.bin",error);if(!personal_owner)return false;
    if(!recover_unlocked(error))return false;
    const auto dir=root/"save-migration-v1";
    if(!storage::writable_path(dir/"manifest.json",error))return false;
    Json existing;
    if(std::filesystem::exists(dir/"manifest.json")) {
        if(!read_json(dir/"manifest.json",existing)||existing.value("schema",0)!=1){error="The saved migration inventory is unreadable; original progress has not been replaced.";return false;}
        return true;
    }
    bool truncated=false;const auto records=stored_adventures(50000,&truncated);
    if(truncated){error="There are too many save candidates for one migration. Export your saves before continuing.";return false;}
    Json index={{"schema",1},{"policy","one-personal-save"},{"created",Timestamp()},{"entries",Json::array()}};
    for(const auto& record:records) {
        if(record.info.size>1024*1024)continue;
        std::vector<std::uint8_t> bytes;
        if(!storage::read_exact(record.info.path,record.info.size,bytes)){error="A save changed during the migration snapshot. No personal save was replaced.";return false;}
        auto id=storage::content_id(bytes);auto image=dir/"images"/(id+".bin");
        std::string guard;if(!storage::writable_path(image,guard)){error=guard;return false;}
        std::vector<std::uint8_t> prior;
        if(std::filesystem::exists(image)&&(!storage::read_exact(image,bytes.size(),prior)||prior!=bytes))image=dir/"images"/(id+"-"+std::to_string(++storage::serial)+".bin");
        if(!std::filesystem::exists(image)&&!storage::write_atomic(image,bytes,nullptr,error))return false;
        const auto source_u8=record.info.path.generic_u8string();
        index["entries"].push_back({{"source",std::string(source_u8.begin(),source_u8.end())},{"image",image.filename().string()},
            {"bytes",bytes.size()},{"hash",id},{"scope",record.scope},{"kind",int(record.kind)},{"valid",record.info.valid},{"preview",record.preview}});
    }
    if(!write_json(dir/"manifest.json",index,error))return false;
    std::fprintf(stderr,"[save][migration] original-images=%zu retained; personal save unchanged\n",records.size());
    return true;
}
bool queue_development_copy(std::string& error) {
    if(!prepare(data_root(),error))return false;
    std::scoped_lock lock(operation_mutex);
    const auto personal=data_root()/"saves/dkr.us.v77.bin";
    auto owner=storage::acquire(personal,error);if(!owner)return false;
    std::vector<std::uint8_t> bytes;
    if(!ReadAdventure(personal,bytes)){error="Your personal save needs recovery before it can be copied to development.";return false;}
    return storage::write_atomic(data_root()/"pending-development-seed.bin",bytes,ReadAdventure,error);
}
bool prepare_development(const std::filesystem::path& root,const std::filesystem::path& folder,std::string& error) {
    if(!prepare(root,error))return false;
    std::scoped_lock lock(operation_mutex);
    // The caller passes the frozen code identity, never an imported file path.
    std::vector<std::string> parts;
    for(const auto& part:folder)parts.push_back(part.string());
    if(parts.size()!=2||parts[0]!="development"||parts[1].size()!=24||
       parts[1].find_first_not_of("0123456789abcdef")!=std::string::npos) {
        error="The offline development save has an invalid owner.";return false;
    }
    const auto target=root/"saves"/folder/"dkr.us.v77.bin";
    const auto pending=root/"pending-development-seed.bin";
    auto owner=storage::acquire(target,error);if(!owner)return false;
    auto request_owner=storage::acquire(pending,error);if(!request_owner)return false;
    std::error_code ec;
    const bool refresh=std::filesystem::exists(pending,ec);
    if(ec){error="Cannot inspect the pending development copy: "+ec.message();return false;}
    const bool exists=std::filesystem::exists(target,ec);
    if(ec){error="Cannot inspect the development save: "+ec.message();return false;}
    std::vector<std::uint8_t> bytes;
    if(exists&&!refresh) {
        if(!ReadAdventure(target,bytes)){error="Your development save needs recovery. It was not replaced; use the confirmed Copy Personal Save to Development action to start a new copy.";return false;}
        std::fprintf(stderr,"[save][development] existing sandbox retained; personal progress unchanged\n");
        return true;
    }
    auto personal_owner=storage::acquire(root/"saves/dkr.us.v77.bin",error);if(!personal_owner)return false;
    auto source=refresh?pending:root/"saves/dkr.us.v77.bin";
    const auto previous=std::filesystem::path(target.string()+".bak");
    if(!refresh&&std::filesystem::exists(previous,ec))source=previous;
    if(ec){error=ec.message();return false;}
    if(!ReadAdventure(source,bytes)) {
        const bool source_exists=std::filesystem::exists(source,ec);
        if(ec||source_exists||refresh||source==previous||std::filesystem::exists(source.string()+".bak",ec)) {
            error="The development seed needs recovery. No original save was replaced.";return false;
        }
        if(ec){error=ec.message();return false;}
        bytes=codec::blank_bytes(); // Only a genuinely new personal Adventure.
    }
    if(exists) {
        std::vector<std::uint8_t> original;
        if(!ReadFileBounded(target,1024*1024,original)){error="The previous development save could not be preserved. No replacement was made.";return false;}
        const auto archive=root/"save-recovery"/("development-original-"+storage::content_id(original)+".bin");
        if(!storage::write_atomic(archive,original,nullptr,error))return false;
    }
    if(!storage::write_atomic(target,bytes,ReadAdventure,error))return false;
    std::vector<std::uint8_t> verified;
    if(!ReadAdventure(target,verified)||verified!=bytes){error="The development copy failed read-back verification. Original saves were kept.";return false;}
    if(refresh) {
        auto archive=root/"save-recovery"/("applied-development-seed-"+Timestamp()+"-"+std::to_string(++storage::serial)+".bin");
        if(!storage::writable_path(pending,error)||!storage::writable_path(archive,error))return false;
        std::filesystem::create_directories(archive.parent_path(),ec);
        if(!ec)std::filesystem::rename(pending,archive,ec);
        if(ec){error="The development copy is safe, but its request could not be archived: "+ec.message();return false;}
    }
    std::fprintf(stderr,"[save][development] %s; personal EEPROM never used as a development write target\n",refresh?"confirmed personal copy installed":"new sandbox seeded from saved progress");
    return true;
}
void unlock(codec::SaveImage& image) {
    image.settings.adventure_two=true;image.settings.drumstick=true;image.settings.tt_trials.fill(true);
}
void complete(codec::SaveImage& image,int selected) {
    for(int i=0;i<int(image.slots.size());++i)if(selected<0||selected==i) {
        auto& slot=image.slots[i];
        slot.course_status.fill(3);slot.taj_flags=0x3f;slot.trophies=0x3ff;slot.bosses=0xfff;
        slot.balloons={47,8,8,8,8,8};slot.tt_amulet=slot.wizpig_amulet=4;
        slot.world_flags.fill(0xffff);slot.keys=0x1e;
        // Mark the known progression cinematics seen without changing the
        // per-slot Adventure Two flag (0x4) or inventing unknown cutscene bits.
        slot.cutscenes|=0x3effb;
        if(codec::sanitise_name(slot.name).find_first_not_of(' ')==std::string::npos)slot.name="DKR";
    }
    unlock(image);
}
bool apply(const codec::SaveImage& image,std::string_view expected,std::string& error) {
    if(!prepare(data_root(),error))return false;
    std::scoped_lock lock(operation_mutex);
    auto owner=storage::acquire(adventure_info().path,error);if(!owner)return false;
    if(revision()!=expected){error="Your save changed since this editor was opened. Reload it before applying edits; no progress was replaced.";return false;}
    if(!codec::validate_editable_ranges(image,&error))return false;
    std::vector<std::uint8_t> original;
    if(!ReadAdventure(adventure_info().path,original)){error="The current personal image is not valid; use Recovery before editing.";return false;}
    codec::SaveImage previous;
    if(!codec::decode(original,previous,&error))return false;
    auto proposed=codec::encode(image);const auto canonical=codec::encode(previous);
    // The manager's record views are read-only. Preserve both retail record
    // blocks byte-for-byte, including opaque/unused initials encodings.
    std::copy(original.begin()+0x80,original.end(),proposed.begin()+0x80);
    // An unchanged slot need not be canonicalised as a side effect of editing
    // another one (notably an erased 0xff empty slot).
    for(unsigned slot=0;slot<3;++slot)if(std::equal(proposed.begin()+slot*40,proposed.begin()+(slot+1)*40,canonical.begin()+slot*40))
        std::copy_n(original.begin()+slot*40,40,proposed.begin()+slot*40);
    if(!codec::validate(proposed,&error))return false;
    std::filesystem::path backup;
    if(!backup_adventure(backup,error)||!storage::write_atomic(adventure_info().path,proposed,ReadAdventure,error))return false;
    std::vector<std::uint8_t> check;
    if(!ReadAdventure(adventure_info().path,check)||check!=proposed){error="The saved edit failed read-back verification. Its backup was retained.";return false;}
    std::fprintf(stderr,"[save][editor] personal EEPROM committed and read-back verified\n");error.clear();return true;
}
bool adopt(const std::filesystem::path& source,bool repair,std::string& error) {
    if(!prepare(data_root(),error))return false;
    std::vector<std::uint8_t> bytes;
    if(!ReadFileBounded(source,512,bytes)||bytes.size()!=512){error="Choose a 512-byte Adventure save.";return false;}
    if(!codec::validate(bytes)) {
        if(!repair){error="This image needs checksum repair. Choose Repair & use copy if you want to attempt recovery.";return false;}
        std::vector<std::uint8_t> fixed;
        if(!codec::repair_checksums(bytes,fixed,&error))return false;
        auto preserved=data_root()/"save-recovery"/("original-"+storage::content_id(bytes)+".bin");
        if(!storage::write_atomic(preserved,bytes,nullptr,error))return false;
        bytes=std::move(fixed);
    }
    const auto staged=data_root()/"save-recovery"/("candidate-"+storage::content_id(bytes)+".bin");
    if(!storage::write_atomic(staged,bytes,ReadAdventure,error))return false;
    return import_adventure(staged,error);
}
bool export_raw(const std::filesystem::path& source,const std::filesystem::path& destination,std::string& error) {
    if(!ExportDestinationAllowed(destination,error))return false;
    std::vector<std::uint8_t> bytes;
    if(!ReadFileBounded(source,1024*1024,bytes)){error="Could not read the original recovery image.";return false;}
    return storage::write_atomic(destination,bytes,nullptr,error);
}
bool import_host(const std::filesystem::path& source,std::string& error) {
    std::vector<std::uint8_t> bytes;
    if(!ReadAdventure(source,bytes)){error="Choose a valid Adventure image for hosting.";return false;}
    if(!prepare(data_root(),error))return false;
    return storage::write_atomic(data_root()/"saves/online/host/dkr.us.v77.bin",bytes,ReadAdventure,error);
}
std::string describe(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes;codec::SaveImage image;
    if(!ReadFileBounded(path,512,bytes)||!codec::decode(bytes,image))return "Not a checksum-valid Adventure image. Checksum repair cannot reconstruct damaged progress.";
    std::string text;
    for(unsigned i=0;i<3;++i) {
        if(i)text+=" | ";const auto& s=image.slots[i];
        text+="Slot "+std::to_string(i+1)+": "+(s.name.empty()?"---":s.name)+" ("+std::to_string(s.balloons[0])+" balloons)";
    }
    text+=" | Drumstick: "+std::string(image.settings.drumstick?"unlocked":"locked");
    text+=" | T.T.: "+std::string(std::all_of(image.settings.tt_trials.begin(),image.settings.tt_trials.end(),[](bool x){return x;})?"unlocked":"locked");
    return text;
}
bool apply_bundle(const std::filesystem::path& source,std::string& error) {
    if(!prepare(data_root(),error))return false;
    std::scoped_lock lock(operation_mutex);
    std::vector<BundleEntry> entries;
    if(!DecodeBundle(source,entries)){error="That file is not a valid DKR-R save bundle.";return false;}
    const auto dir=data_root()/"pending-save-transaction";
    Json journal={{"schema",1},{"state","prepared"},{"items",Json::array()}};
    // A crash while preserving originals has an empty rollback set: no live
    // destination is replaced until the complete prepared journal is durable.
    if(!write_json(dir/"journal.json",journal,error))return false;
    std::vector<std::shared_ptr<storage::Lease>> owners;
    for(std::size_t i=0;i<entries.size();++i) {
        const auto& entry=entries[i];Json item={{"kind",entry.kind},{"channel",entry.channel}};
        const auto dest=transaction_destination(item);
        auto owner=storage::acquire(dest,error);if(!owner)return false;owners.push_back(std::move(owner));
        std::vector<std::uint8_t> original;std::error_code ec;const bool exists=std::filesystem::exists(dest,ec);
        if(ec){error=ec.message();return false;}
        if(exists&&!ReadFileBounded(dest,1024*1024,original)){error="Could not preserve a destination before import.";return false;}
        if(exists&&!storage::write_atomic(dir/(std::to_string(i)+".original"),original,nullptr,error))return false;
        item["existed"]=exists;item["bytes"]=original.size();item["hash"]=storage::content_id(original);journal["items"].push_back(std::move(item));
    }
    if(!write_json(dir/"journal.json",journal,error))return false;
    for(std::size_t i=0;i<entries.size();++i) {
        const auto validator=entries[i].kind==kBundleAdventureKind?ReadAdventure:entries[i].kind==kBundleRecordsKind?ReadCustomRecords:ReadControllerPak;
        if(!storage::write_atomic(transaction_destination(journal["items"][i]),entries[i].bytes,validator,error)) {
            const auto cause=error;std::string rollback;
            if(!recover_unlocked(rollback))error=cause+" Recovery is incomplete: "+rollback;
            else error=cause+" The previous images were restored and retained.";
            return false;
        }
    }
    journal["state"]="committed";
    if(!write_json(dir/"journal.json",journal,error))return false;
    return retire_transaction(dir,error);
}
bool reset(std::string& error) {
    if(!prepare(data_root(),error))return false;
    std::vector<BundleEntry> entries{{kBundleAdventureKind,0,codec::blank_bytes()},
        {kBundleRecordsKind,0,records::encode({})}};
    std::vector<std::uint8_t> bytes(kBundleMagic.begin(),kBundleMagic.end());
    AppendLE32(bytes,kBundleVersion);AppendLE32(bytes,entries.size());
    for(const auto& entry:entries) {
        AppendLE32(bytes,entry.kind);AppendLE32(bytes,entry.channel);AppendLE32(bytes,entry.bytes.size());
        AppendLE32(bytes,ImageChecksum(entry.bytes));bytes.insert(bytes.end(),entry.bytes.begin(),entry.bytes.end());
    }
    const auto source=data_root()/"save-recovery"/("fresh-"+Timestamp()+".dkrsave");
    if(!storage::write_atomic(source,bytes,ReadBundle,error))return false;
    return apply_bundle(source,error);
}
}

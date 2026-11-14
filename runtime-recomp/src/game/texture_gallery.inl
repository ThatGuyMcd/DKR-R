// Worker-only, read-only browser. Never constructs a writable Workspace or
// rebuilds its export indexes when changing pages. Selected PNG paths still
// pass the same member()/link checks before any filesystem read.
struct GalleryRequest {
    std::filesystem::path folder;
    std::string search;
    unsigned page=0;
    bool reload=false;
    std::uint64_t serial=0;
};
struct GalleryResult {
    std::filesystem::path folder;
    std::string revision;
    std::vector<Thumbnail> thumbnails;
    unsigned page=0,pages=0;
    std::uint64_t matches=0;
};
struct GalleryFileStamp {
    std::uintmax_t bytes=0;
    std::filesystem::file_time_type modified{};
    bool operator==(const GalleryFileStamp&)const=default;
};
GalleryFileStamp gallery_stamp(const std::filesystem::path& path) {
    if(link(path)||!std::filesystem::is_regular_file(path))throw Error("A texture browser file is missing or is a link.");
    return {std::filesystem::file_size(path),std::filesystem::last_write_time(path)};
}
class GalleryCache {
    struct Record {Thumbnail item;std::string searchable;};
    struct Preview {
        GalleryFileStamp stamp;
        unsigned width=0,height=0;
        Bytes rgba;
        std::uint64_t used=0;
    };
    std::filesystem::path root_;
    GalleryFileStamp stamp_;
    std::string revision_,search_;
    std::vector<Record> records_;
    std::vector<std::size_t> matches_;
    std::map<std::string,Preview> previews_;
    bool indexed_=false,filtered_=false;
    std::uint64_t clock_=0,index_generation_=0;
    static std::string lower(std::string text) {
        for(auto& c:text)if(c>='A'&&c<='Z')c+=32;
        return text;
    }
public:
    static constexpr unsigned page_size=24,preview_capacity=128;
    std::optional<GalleryResult> load(const GalleryRequest& request,const std::function<bool()>& current) {
        const auto start=Clock::now();
        const auto root=private_storage_path(request.folder);
        if(link(root))throw Error("The dump folder is a link.");
        const auto manifest_stamp=gallery_stamp(root/"dump.json");
        const bool rebuild=request.reload||!indexed_||root!=root_||manifest_stamp!=stamp_;
        if(rebuild) {
            // Writer/export calls keep the default full validation. This
            // read-only index validates file paths lazily on the selected page.
            auto doc=read_manifest(root,false);
            std::vector<Record> records;records.reserve(doc["images"].size());
            for(std::size_t index=0;index<doc["images"].size();++index) {
                if(index%128==0&&!current())return {};
                const auto& image=doc["images"][index];Record r;
                r.item.name=image.at("name").get<std::string>();r.item.owner=image.at("owner").get<std::string>();
                r.item.path=image.at("original").get<std::string>();r.item.status=image.at("status").get<std::string>();
                r.item.width=image.at("width");r.item.height=image.at("height");r.item.frame=image.value("frame",0U);
                r.searchable=lower(r.item.owner+" "+r.item.name);records.push_back(std::move(r));
            }
            if(!current())return {};
            // Do not cache a manifest replaced while its index was being read.
            if(gallery_stamp(root/"dump.json")!=manifest_stamp)throw Error("The dump changed while loading. Refresh the browser.");
            root_=root;stamp_=manifest_stamp;revision_=doc.value("rom_revision","");
            records_=std::move(records);previews_.clear();indexed_=true;filtered_=false;++index_generation_;
        }
        const auto search=lower(request.search);
        if(!filtered_||search!=search_) {
            std::vector<std::size_t> matches;matches.reserve(records_.size());
            for(std::size_t index=0;index<records_.size();++index) {
                if(index%128==0&&!current())return {};
                if(search.empty()||records_[index].searchable.find(search)!=std::string::npos)matches.push_back(index);
            }
            matches_=std::move(matches);search_=search;filtered_=true;
        }
        GalleryResult result;result.folder=root_;result.revision=revision_;result.matches=matches_.size();
        result.pages=unsigned((matches_.size()+page_size-1)/page_size);
        result.page=result.pages?std::min(request.page,result.pages-1):0;
        unsigned reads=0,hits=0;
        for(std::size_t index=std::size_t(result.page)*page_size;index<std::min(matches_.size(),std::size_t(result.page+1)*page_size);++index) {
            if(!current())return {};
            auto item=records_[matches_[index]].item;
            try {
                const auto path=member(root_,item.path);
                const auto stamp=gallery_stamp(path);
                auto found=previews_.find(item.path);
                if(found==previews_.end()||found->second.stamp!=stamp) {
                    auto bytes=read_file(path,64*MiB);int width=0,height=0,channels=0;
                    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels)||width<=0||height<=0||std::uint64_t(width)*height>16*MiB)
                        throw Error("Invalid preview image dimensions.");
                    auto* decoded=stbi_load_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,4);
                    if(!decoded)throw Error("Could not read this texture preview.");
                    struct Free{unsigned char* p;~Free(){stbi_image_free(p);}}free{decoded};
                    Preview preview;preview.stamp=stamp;
                    preview.width=width>=height?96:std::max(1,96*width/height);
                    preview.height=height>=width?96:std::max(1,96*height/width);
                    preview.rgba.resize(preview.width*preview.height*4);
                    for(unsigned y=0;y<preview.height;++y)for(unsigned x=0;x<preview.width;++x)
                        std::memcpy(preview.rgba.data()+(y*preview.width+x)*4,decoded+((std::size_t(y)*height/preview.height)*width+x*width/preview.width)*4,4);
                    if(gallery_stamp(path)!=stamp)throw Error("This preview changed while loading. Refresh to try again.");
                    if(found==previews_.end()&&previews_.size()>=preview_capacity) {
                        auto oldest=std::min_element(previews_.begin(),previews_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                        previews_.erase(oldest);
                    }
                    found=previews_.insert_or_assign(item.path,std::move(preview)).first;++reads;
                }else ++hits;
                auto& preview=found->second;preview.used=++clock_;
                item.preview_width=preview.width;item.preview_height=preview.height;item.rgba=preview.rgba;
                item.preview_key=text_path(root_)+"/"+item.path+":"+std::to_string(index_generation_)+":"+std::to_string(stamp.bytes)+":"+std::to_string(static_cast<std::int64_t>(stamp.modified.time_since_epoch().count()));
            }catch(const std::exception& e) {
                // One damaged PNG must not prevent navigation through the dump.
                item.preview_error=e.what();
            }
            result.thumbnails.push_back(std::move(item));
        }
        if(!current())return {};
        const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        std::fprintf(stderr,"[texture-browser] page=%u/%u matches=%zu index-rebuilt=%u PNG-reads=%u cache-hits=%u elapsed=%.2fms\n",
            result.pages?result.page+1:0,result.pages,matches_.size(),unsigned(rebuild),reads,hits,elapsed);
        return result;
    }
};

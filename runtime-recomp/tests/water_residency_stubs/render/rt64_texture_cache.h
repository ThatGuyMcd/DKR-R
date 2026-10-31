#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
namespace RT64 {
enum class ReplacementOperation { Stream, Preload, Stall };
struct ReplacementResolvedPath {
    std::uint32_t fileSystemIndex = 0;
    std::uint64_t hash = 0;
    std::string relativePath;
    ReplacementOperation resolvedOperation = ReplacementOperation::Stream;
};
struct ReplacementMap {
    int texture = 1;
    std::vector<int> fileSystems{1};
    std::vector<std::unordered_map<std::uint64_t, ReplacementResolvedPath>> fileSystemResolvedPaths{1};
    std::set<std::pair<unsigned, std::string>> resident;
    int* getFromRelativePath(unsigned layer, const std::string& path) {
        return resident.contains({layer, path}) ? &texture : nullptr;
    }
};
struct TextureMap {
    ReplacementMap replacementMap;
};
struct TextureCache {
    TextureMap textureMap;
    std::mutex textureMapMutex, uploadQueueMutex;
    std::vector<ReplacementResolvedPath> resolvedPathQueue;
    std::condition_variable uploadQueueChanged;
    std::uint64_t generation = 1;
    std::uint64_t getReplacementGeneration() const {
        return generation;
    }
};
struct State {
    struct {
        TextureCache* textureCache = nullptr;
    } ext;
    std::uint8_t* RDRAM = nullptr;
};
} // namespace RT64

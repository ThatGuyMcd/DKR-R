#pragma once
#include "hle/rt64_application.h"
#include "render/rt64_generated_mip_config.h"
#include "runtime_ui.hpp"
#include <mutex>

namespace dkr::runtime {
// Called only by the renderer thread, with its presentation mutex held, while
// TextureCache has RELEASED the upload lock. Never submit/replay a game workload.
inline void present_mipmap_loading(RT64::Application& app) {
    if (!RT64::mipLoadingVisible() || !app.presentQueue || !app.sharedQueueResources ||
        app.sharedQueueResources->totalPresentations.load(std::memory_order_acquire) == 0)
        return;
    auto& present = *app.presentQueue;
    std::unique_lock threadLock(present.threadMutex, std::try_to_lock);
    if (!threadLock.owns_lock())
        return;
    auto* swap = present.ext.swapChain;
    if (!swap || swap->isEmpty() || swap->needsResize() || !present.acquiredSemaphore ||
        present.drawSemaphores.size() < swap->getTextureCount() ||
        present.swapChainFramebuffers.size() < swap->getTextureCount())
        return;
    // Same worker, acquire semaphore and per-image draw semaphores as normal
    // presents. The present-thread mutex serializes all swap-chain ownership.
    // Initialization is proven by the completed-present counter above.
    ui::draw(app);
    std::scoped_lock inspectorLock(present.inspectorMutex);
    if (!present.inspector)
        return;
    uint32_t index = 0;
    if (!swap->acquireTexture(present.acquiredSemaphore.get(), &index))
        return;
    using namespace plume;
    auto* texture = swap->getTexture(index);
    auto* worker = present.ext.presentGraphicsWorker;
    auto* cmd = worker->commandList.get();
    cmd->begin();
    cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture, RenderTextureLayout::COLOR_WRITE));
    cmd->setFramebuffer(present.swapChainFramebuffers[index].get());
    cmd->clearColor();
    present.inspector->draw(cmd);
    cmd->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(texture, RenderTextureLayout::PRESENT));
    cmd->end();
    const RenderCommandList* list = cmd;
    auto* wait = present.acquiredSemaphore.get();
    auto* signal = present.drawSemaphores[index].get();
    worker->commandQueue->executeCommandLists(&list, 1, &wait, 1, &signal, 1, worker->commandFence.get());
    worker->wait(); // Existing single UI submission fence, never texture-worker/device idle.
    swap->present(index, &signal, 1);
    // Deliberately leave VI history, ring cursors, workload IDs and simulation counters alone.
}
} // namespace dkr::runtime

// Included by the checked renderer adapter. Called only by its SDL/render
// owner, under the same presentation ownership as update_config.
void ApplyDisplayRequest(RT64::Application& application,bool notify) {
    using namespace dkr::display;
    unsigned width=application.sharedQueueResources?application.sharedQueueResources->swapChainWidth:0;
    unsigned height=application.sharedQueueResources?application.sharedQueueResources->swapChainHeight:0;
    if(!width||!height) {
        int w=0,h=0;
        if(auto* window=static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()))SDL_GetWindowSize(window,&w,&h);
        width=std::max(1,w);height=std::max(1,h);
    }
    output_width=width;output_height=height;
    if(application.device)if(const auto limit=application.device->getCapabilities().maxTextureSize)device_limit=unsigned(std::min<std::uint64_t>(limit,DimensionLimit));
    if(!dkr::runtime::enhancements::modern_presentation_enabled())return;
    const auto value=resolve(*published.load(),width,height);
    auto& config=application.userConfig;
    const bool changed=config.aspectRatio!=RT64::UserConfiguration::AspectRatio::Manual ||
        std::abs(config.aspectTarget-value.aspect)>0.000001 ||
        config.resolution!=RT64::UserConfiguration::Resolution::Manual ||
        std::abs(config.resolutionMultiplier-value.scale)>0.000001 ||
        config.downsampleMultiplier!=value.downsample;
    config.aspectRatio=RT64::UserConfiguration::AspectRatio::Manual;
    config.aspectTarget=value.aspect;
    config.resolution=RT64::UserConfiguration::Resolution::Manual;
    config.resolutionMultiplier=value.scale;
    config.downsampleMultiplier=value.downsample;
    dkr::runtime::enhancements::set_fit_to_window_enabled(std::abs(value.aspect-4./3.)>0.000001);
    if(changed) {
        if(notify)application.updateUserConfig(true);
        std::fprintf(stderr,"[graphics][display] render=%ux%u output=%ux%u aspect=%.6f scale=%.6f downsample=%d\n",
            value.render.width,value.render.height,width,height,value.aspect,value.scale,value.downsample);
    }
}

"""User-approved, four-file VR device hook, ONLY on a new RT64 build copy.

No OpenXR policy or camera logic is embedded in RT64. The host can supply an
optional Vulkan factory and per-interface device/extension requirements. Empty
configuration follows the original backend unchanged. Never apply in extern.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from camera_plus_pipeline import new_output, replace, digest

PINS = {
    'src/hle/rt64_application.h': '7d36ce05c422d6d537aebcf1577560d65a304d6c48f53a8e8de321ff12abf27b',
    'src/hle/rt64_application.cpp': '5bbb4960a262c91b1ccc383ed0027b25c60be839f7d38ed0f205a9fcd0341f84',
    'src/contrib/plume/plume_vulkan.h': 'ca8f3abc3507e61da6e85e7acc30168ec6cc41adf7a368db1947cb6995554977',
    'src/contrib/plume/plume_vulkan.cpp': '70ecc88658c7231497836d15ea9f98d80861a6e991cc6128d40c7ac980193038'
}


def transform(name, text):
    if name == 'src/hle/rt64_application.h':
        text = replace(text, '#include <filesystem>', '#include <filesystem>\n#include <functional>')
        return replace(text, '        bool useConfigurationFile = true;', '''        bool useConfigurationFile = true;
        // Optional host-owned device negotiation, e.g. an OpenXR Vulkan GPU.
        // No callback preserves the original backend/device selection.
        std::function<std::unique_ptr<RenderInterface>(RenderWindow)> vulkanInterfaceFactory;''')
    if name == 'src/hle/rt64_application.cpp':
        return replace(text, 'renderInterface = CreateVulkanInterfaceWrapper(appWindow->windowHandle);',
                       '''renderInterface = appConfig.vulkanInterfaceFactory ?
                    appConfig.vulkanInterfaceFactory(appWindow->windowHandle) :
                    CreateVulkanInterfaceWrapper(appWindow->windowHandle);''', 2)
    if name == 'src/contrib/plume/plume_vulkan.h':
        text = replace(text, '#include <mutex>', '#include <mutex>\n#include <functional>')
        text = replace(text, '    struct VulkanInterface : RenderInterface {', '''    // Immutable per-interface requirements, not process-global hooks. Used
    // only before graphics workers exist. The callback must return a device
    // belonging to this VkInstance; a null/foreign device fails creation.
    struct VulkanExternalConfiguration {
        std::vector<std::string> instanceExtensions, deviceExtensions;
        std::function<VkPhysicalDevice(VkInstance)> selectPhysicalDevice;
        std::function<VkResult(const VkInstanceCreateInfo&, VkInstance*)> createInstance;
        std::function<VkResult(VkPhysicalDevice, const VkDeviceCreateInfo&, VkDevice*)> createDevice;
        uint32_t apiVersion = VK_API_VERSION_1_2;
    };
    struct VulkanInterface : RenderInterface {
        VulkanExternalConfiguration externalConfiguration;''')
        text = replace(text, '        VulkanInterface(RenderWindow sdlWindow);', '''        VulkanInterface(RenderWindow sdlWindow);
        VulkanInterface(RenderWindow sdlWindow, const VulkanExternalConfiguration &configuration);''')
        return replace(text, '        VulkanInterface();', '''        VulkanInterface();
        explicit VulkanInterface(const VulkanExternalConfiguration &configuration);''')
    if name == 'src/contrib/plume/plume_vulkan.cpp':
        text = replace(text, '        uint32_t currentDeviceTypeScore = 0;', '''        VkPhysicalDevice requiredPhysicalDevice = VK_NULL_HANDLE;
        if (renderInterface->externalConfiguration.selectPhysicalDevice) {
            requiredPhysicalDevice = renderInterface->externalConfiguration.selectPhysicalDevice(renderInterface->instance);
            if (requiredPhysicalDevice == VK_NULL_HANDLE ||
                std::find(physicalDevices.begin(), physicalDevices.end(), requiredPhysicalDevice) == physicalDevices.end()) {
                fprintf(stderr, "External graphics runtime did not select a valid Vulkan device.\\n");
                return;
            }
        }
        uint32_t currentDeviceTypeScore = 0;''')
        text = replace(text, '        for (uint32_t i = 0; i < deviceCount; i++) {\n            VkPhysicalDeviceProperties deviceProperties;', '''        for (uint32_t i = 0; i < deviceCount; i++) {
            if (requiredPhysicalDevice != VK_NULL_HANDLE && physicalDevices[i] != requiredPhysicalDevice) continue;''')
        text = replace(text, '            if (requiredPhysicalDevice != VK_NULL_HANDLE && physicalDevices[i] != requiredPhysicalDevice) continue;',
                       '            if (requiredPhysicalDevice != VK_NULL_HANDLE && physicalDevices[i] != requiredPhysicalDevice) continue;\n            VkPhysicalDeviceProperties deviceProperties;')
        text = replace(text, '            bool preferOption = preferDeviceTypeScore || preferUserChoice;',
                       '            bool preferOption = requiredPhysicalDevice != VK_NULL_HANDLE || preferDeviceTypeScore || preferUserChoice;')
        text = replace(text, '        std::unordered_set<std::string> missingRequiredExtensions = RequiredDeviceExtensions;', '''        std::unordered_set<std::string> requiredDeviceExtensions = RequiredDeviceExtensions;
        requiredDeviceExtensions.insert(renderInterface->externalConfiguration.deviceExtensions.begin(),
            renderInterface->externalConfiguration.deviceExtensions.end());
        std::unordered_set<std::string> missingRequiredExtensions = requiredDeviceExtensions;''')
        text = replace(text, '        for (const std::string &extension : RequiredDeviceExtensions) {',
                       '        for (const std::string &extension : requiredDeviceExtensions) {')
        # XR requirements can also be Plume optional extensions. Preserve the
        # feature discovery, but never enable an extension name twice.
        optional_loop = '''        for (const std::string &extension : supportedOptionalExtensions) {
            enabledExtensions.push_back(extension.c_str());
        }'''
        for required in ('requiredDeviceExtensions', 'requiredExtensions'):
            prefix = '''        for (const std::string &extension : %s) {
            enabledExtensions.push_back(extension.c_str());
        }

''' % required
            text = replace(text, prefix + optional_loop, prefix + '''        for (const std::string &extension : supportedOptionalExtensions) {
            if (%s.find(extension) == %s.end())
                enabledExtensions.push_back(extension.c_str());
        }''' % (required, required))
        text = replace(text, '''    VulkanInterface::VulkanInterface(RenderWindow sdlWindow) {
#else
    VulkanInterface::VulkanInterface() {
#endif''', '''    VulkanInterface::VulkanInterface(RenderWindow sdlWindow)
        : VulkanInterface(sdlWindow, VulkanExternalConfiguration{}) {}
    VulkanInterface::VulkanInterface(RenderWindow sdlWindow, const VulkanExternalConfiguration &configuration)
        : externalConfiguration(configuration) {
#else
    VulkanInterface::VulkanInterface() : VulkanInterface(VulkanExternalConfiguration{}) {}
    VulkanInterface::VulkanInterface(const VulkanExternalConfiguration &configuration)
        : externalConfiguration(configuration) {
#endif''')
        text = replace(text, '        VkResult res = vkCreateDevice(physicalDevice, &createInfo, nullptr, &vk);', '''        VkResult res = renderInterface->externalConfiguration.createDevice ?
            renderInterface->externalConfiguration.createDevice(physicalDevice, createInfo, &vk) :
            vkCreateDevice(physicalDevice, &createInfo, nullptr, &vk);''')
        text = replace(text, '        res = vkCreateInstance(&createInfo, nullptr, &instance);', '''        res = externalConfiguration.createInstance ?
            externalConfiguration.createInstance(createInfo, &instance) :
            vkCreateInstance(&createInfo, nullptr, &instance);''')
        text = replace(text, '        appInfo.apiVersion = VK_API_VERSION_1_2;',
                       '        appInfo.apiVersion = externalConfiguration.apiVersion;')
        return replace(text, '        std::unordered_set<std::string> requiredExtensions = RequiredInstanceExtensions;', '''        std::unordered_set<std::string> requiredExtensions = RequiredInstanceExtensions;
        requiredExtensions.insert(externalConfiguration.instanceExtensions.begin(), externalConfiguration.instanceExtensions.end());''')
    raise ValueError('VR patch attempted an unapproved dependency path')


def stage(source, output):
    source = source.resolve(strict=True)
    output = new_output(output, source)
    transformed = {}
    for name, pin in PINS.items():
        if digest(source / name) != pin:
            raise ValueError('Unreviewed renderer input: ' + name)
        transformed[name] = transform(name, (source / name).read_text())
    # A full separate source copy keeps every header consumer ABI-consistent.
    # Git administration is not copied and no dependency checkout is patched.
    shutil.copytree(source, output, ignore=shutil.ignore_patterns('.git', '__pycache__'))
    originals = {}
    for path in sorted(output.rglob('*')):
        if path.is_symlink():
            raise ValueError('Renderer build copy must not contain symbolic links')
        if path.is_file():
            originals[path.relative_to(output).as_posix()] = digest(path)
    for name in PINS:
        (output / name).write_text(transformed[name], newline='\n')
    receipt = {'schema': 1, 'source': source.as_posix(), 'pins': PINS, 'originals': originals,
               'changed': {name: digest(output / name) for name in PINS}}
    (output / 'camera-vr-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('User-approved four-file VR hook staged; original submodules unchanged:', output)


def verify(output):
    receipt = json.loads((output / 'camera-vr-receipt.json').read_text())
    source = Path(receipt['source'])
    if receipt['schema'] != 1 or receipt['pins'] != PINS:
        raise ValueError('Unknown VR dependency composition')
    actual = {p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()}
    if actual != set(receipt['originals']) | {'camera-vr-receipt.json'}:
        raise ValueError('VR renderer source file set changed')
    for name, original in receipt['originals'].items():
        if digest(source / name) != original:
            raise ValueError('Original renderer input changed: ' + name)
        path = output / name
        if name in PINS:
            if digest(source / name) != PINS[name] or path.read_text() != transform(name, (source / name).read_text()):
                raise ValueError('Unreviewed staged VR edit: ' + name)
        elif digest(path) != original:
            raise ValueError('Unapproved dependency edit: ' + name)
    print('VR dependency composition inspected: exactly four approved files differ')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['stage', 'verify'])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.operation == 'stage':
        if not args.source:
            parser.error('stage requires --source')
        stage(args.source, args.output)
    else:
        verify(args.output.resolve(strict=True))

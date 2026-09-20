window.DKRLauncher = window.DKRLauncher || {};

DKRLauncher.config = {
  version: '1.0.5-beta.8',
  pages: [
    { id: 'play', label: 'PLAY', variant: 'green' },
    { id: 'graphics', label: 'GRAPHICS' },
    { id: 'sound', label: 'SOUND' },
    { id: 'controls', label: 'CONTROLS' },
    { id: 'saves', label: 'SAVE MANAGER' },
    { id: 'textures', label: 'TEXTURES' },
    { id: 'mods', label: 'MODS / HACKS' },
    { id: 'online', label: 'DKR-R ONLINE' },
    { id: 'about', label: 'ABOUT DKR-R' },
  ],
  links: {
    github: 'https://github.com/ThatGuyMcd/DKR-R',
    discord: 'https://discord.gg/AMWfXdBjNP',
  },
  credits: [
    { name: 'N64Recomp', url: 'https://github.com/N64Recomp/N64Recomp' },
    { name: 'N64ModernRuntime', url: 'https://github.com/N64Recomp/N64ModernRuntime' },
    { name: 'RT64', url: 'https://github.com/rt64/rt64' },
    { name: 'Monocypher', url: 'https://github.com/LoupVaillant/Monocypher' },
    { name: 'Mbed TLS', url: 'https://github.com/Mbed-TLS/mbedtls' },
    { name: 'GekkoNet', url: 'https://github.com/HeatXD/GekkoNet' },
  ],
};

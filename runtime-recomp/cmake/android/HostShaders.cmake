# Plume is an RT64 child. Select the shader compiler for the BUILD HOST, not
# Android's CPU. The setting goes only to that RT64 parent directory.
if(ANDROID)
    if(CMAKE_HOST_WIN32)
        set(DXC "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc.exe" PARENT_SCOPE)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(DXC "LD_LIBRARY_PATH=${DKR_RT64_SOURCE}/src/contrib/dxc/lib/x64"
            "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc-linux" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Android host shader tools have not been qualified on this host.")
    endif()
endif()

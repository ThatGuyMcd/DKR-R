# Included in NFD's directory scope only. Its upstream UNIX branch assumes GTK.
# Let it declare the public target/header, then attach our Android adapter.
if(ANDROID)
    set(UNIX FALSE)
endif()

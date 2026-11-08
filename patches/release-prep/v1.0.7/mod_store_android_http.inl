// Android's platform TLS stack; same URL, credential, redirect and sink policy.
// Included inside the Store implementation's private namespace.
Response http_once(const Request& r, std::stop_token stop, const Sink& sink) {
    if (!trusted_url(r.url, prefix(r.url, Api)))
        throw Error("The library returned an unapproved file address.");
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || env->PushLocalFrame(64) < 0) {
        if (env && env->ExceptionCheck()) env->ExceptionClear();
        throw Error("The secure Android connection could not start.");
    }
    struct Frame { JNIEnv* env; ~Frame(){ env->PopLocalFrame(nullptr); } } frame{env};
    auto check = [&] {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            throw Error("The secure connection was interrupted. Check your connection and retry.");
        }
        if (stop.stop_requested()) throw Error("Transfer paused.");
    };
    auto cls = [&](const char* name) {
        auto value=env->FindClass(name);check();
        if(!value)throw Error("The secure Android connection is unavailable.");
        return value;
    };
    auto method = [&](jclass owner,const char* name,const char* signature) {
        auto value=env->GetMethodID(owner,name,signature);check();
        if(!value)throw Error("The secure Android connection is unavailable.");
        return value;
    };
    auto string = [&](const std::string& text) {
        auto value=env->NewStringUTF(text.c_str());check();
        if(!value)throw Error("The connection ran out of memory.");
        return value;
    };
    const auto url_class=cls("java/net/URL");
    const auto http_class=cls("javax/net/ssl/HttpsURLConnection");
    const auto input_class=cls("java/io/InputStream");
    const auto output_class=cls("java/io/OutputStream");
    const auto url=env->NewObject(url_class,method(url_class,"<init>","(Ljava/lang/String;)V"),string(r.url));check();
    const auto connection=env->CallObjectMethod(url,method(url_class,"openConnection","()Ljava/net/URLConnection;"));check();
    if(!connection || !env->IsInstanceOf(connection,http_class))throw Error("Use the secure library address.");
    const auto disconnect=method(http_class,"disconnect","()V");
    struct Connection {
        JNIEnv* env; jobject value; jmethodID disconnect;
        ~Connection(){env->CallVoidMethod(value,disconnect);if(env->ExceptionCheck())env->ExceptionClear();}
    } cleanup{env,connection,disconnect};
    auto set_int=[&](const char* name,int value){env->CallVoidMethod(connection,method(http_class,name,"(I)V"),jint(value));check();};
    env->CallVoidMethod(connection,method(http_class,"setInstanceFollowRedirects","(Z)V"),JNI_FALSE);check();
    env->CallVoidMethod(connection,method(http_class,"setUseCaches","(Z)V"),JNI_FALSE);check();
    set_int("setConnectTimeout",4000);
    set_int("setReadTimeout",int(std::clamp(r.timeout,1000U,4000U)));
    env->CallVoidMethod(connection,method(http_class,"setRequestMethod","(Ljava/lang/String;)V"),string(r.method));check();
    const auto set_header=method(http_class,"setRequestProperty","(Ljava/lang/String;Ljava/lang/String;)V");
    auto header=[&](const std::string& key,const std::string& value) {
        if(value.find_first_of("\r\n")!=std::string::npos)throw Error("Invalid connection header.");
        auto k=string(key),v=string(value);
        env->CallVoidMethod(connection,set_header,k,v);check();
        env->DeleteLocalRef(k);env->DeleteLocalRef(v);
    };
    header("User-Agent","DKR-R-ModStore/1.0.7");header("Accept","*/*");
    header("Accept-Encoding","identity");
    if(prefix(r.url,Api)) {
        header("Origin","https://positron-portal.com");
        if(!r.token.empty()) {
            if(r.token.size()!=43 || !identifier(r.token))throw Error("Sign in again to continue.");
            header("Authorization","Bearer "+r.token);
        }
    } else {
        if(!r.token.empty())throw Error("Credentials cannot be sent to a file host.");
        header("Referer","https://positron-portal.com/dkr-r/index.html");
    }
    if(r.offset) {
        header("Range","bytes="+std::to_string(r.offset)+"-");
        if(!r.etag.empty() && r.etag.find_first_of("\r\n")==std::string::npos)header("If-Range",r.etag);
    }
    if(r.method=="POST") {
        if(r.body.size()>MaxJson)throw Error("The library request exceeds its allowed size.");
        header("Content-Type",r.type);
        env->CallVoidMethod(connection,method(http_class,"setDoOutput","(Z)V"),JNI_TRUE);check();
        set_int("setFixedLengthStreamingMode",int(r.body.size()));
        auto output=env->CallObjectMethod(connection,method(http_class,"getOutputStream","()Ljava/io/OutputStream;"));check();
        if(!output)throw Error("The library request could not be sent.");
        const auto write=method(output_class,"write","([BII)V");
        auto block=env->NewByteArray(65536);check();
        for(std::size_t at=0;at<r.body.size();) {
            check();const auto count=jsize(std::min<std::size_t>(65536,r.body.size()-at));
            env->SetByteArrayRegion(block,0,count,reinterpret_cast<const jbyte*>(r.body.data()+at));check();
            env->CallVoidMethod(output,write,block,0,count);check();at+=count;
        }
        env->CallVoidMethod(output,method(output_class,"close","()V"));check();
        env->DeleteLocalRef(block);env->DeleteLocalRef(output);
    }
    Response response;response.url=r.url;
    response.status=unsigned(env->CallIntMethod(connection,method(http_class,"getResponseCode","()I")));check();
    const auto get_header=method(http_class,"getHeaderField","(Ljava/lang/String;)Ljava/lang/String;");
    std::size_t header_bytes=0;
    auto field=[&](const char* key) {
        auto k=string(key);
        auto value=static_cast<jstring>(env->CallObjectMethod(connection,get_header,k));check();env->DeleteLocalRef(k);
        if(!value)return std::string{};
        const auto length=env->GetStringUTFLength(value);check();
        if(length<0 || std::size_t(length)>32768-header_bytes)throw Error("The library response headers are too large.");
        header_bytes+=std::size_t(length);
        const auto* bytes=env->GetStringUTFChars(value,nullptr);check();
        if(!bytes)throw Error("The connection ran out of memory.");
        std::string result(bytes);env->ReleaseStringUTFChars(value,bytes);env->DeleteLocalRef(value);return result;
    };
    response.type=lower(field("Content-Type"));response.etag=field("ETag");
    response.range=field("Content-Range");response.location=field("Location");
    const auto length=field("Content-Length");
    if(!length.empty()) {
        if(length.size()>20 || !std::all_of(length.begin(),length.end(),[](char c){return c>='0'&&c<='9';}))throw Error("Invalid download size.");
        try{response.length=std::stoull(length);}catch(...){throw Error("Invalid download size.");}
    }
    // Redirects are followed only by the existing approved-host outer policy.
    if(response.status>=300 && response.status<400)return response;
    auto input=env->CallObjectMethod(connection,method(http_class,response.status>=400?"getErrorStream":"getInputStream","()Ljava/io/InputStream;"));check();
    if(!input)return response;
    const auto read=method(input_class,"read","([B)I");
    auto block=env->NewByteArray(65536);check();
    std::array<char,65536> bytes{};std::uint64_t total=0;
    for(;;) {
        check();const auto count=env->CallIntMethod(input,read,block);check();
        if(count<0)break;
        if(!count)continue;
        if(std::uint64_t(count)>r.limit-total)throw Error("The library response exceeds its allowed size.");
        total+=std::uint64_t(count);
        env->GetByteArrayRegion(block,0,count,reinterpret_cast<jbyte*>(bytes.data()));check();
        if(sink && response.status>=200 && response.status<300)sink(bytes.data(),std::size_t(count),response);
        else if(response.body.size()+count<=std::min<std::uint64_t>(r.limit,MaxImage))response.body.append(bytes.data(),std::size_t(count));
        else throw Error("The library response exceeds its allowed size.");
    }
    env->CallVoidMethod(input,method(input_class,"close","()V"));check();
    return response;
}

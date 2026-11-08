"""Compose Android-only Store TLS without modifying the accepted desktop input."""
from pathlib import Path
import argparse
import hashlib
import json
import re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
auth=Path(__file__).resolve().parent
source=a.source.read_text(encoding='utf-8')
old='#else\n#include <curl/curl.h>'
assert source.count(old)==1
source=source.replace(old,'#elif defined(__ANDROID__)\n#include <jni.h>\n#include <SDL_system.h>\n#include <fcntl.h>\n#include <unistd.h>\n#else\n#include <curl/curl.h>')
at=source.index('    static const bool initialized = [] { return curl_global_init')
begin=source.rindex('#else',0,at)
source=source[:begin]+'#elif defined(__ANDROID__)\n#include "mod_store_android_http.inl"\n'+source[begin:]
a.output.mkdir(parents=True,exist_ok=True)
sources={'mod_store.cpp':source}
for name in ('mod_store_install.cpp','mod_store_names.cpp'):
    sources[name]=(a.source.parent/name).read_text(encoding='utf-8')
for name,text in sources.items():
    text='#include "android_store_compat.hpp"\n'+text
    text=text.replace('std::atomic<std::shared_ptr<const Snapshot>>','std::shared_ptr<const Snapshot>')
    text=text.replace('std::atomic<std::shared_ptr<const InstallView>>','std::shared_ptr<const InstallView>')
    text=re.sub(r'((?:[A-Za-z_][A-Za-z_0-9]*(?:\.|->))*published)\.load\(\)',r'std::atomic_load(&\1)',text)
    text=text.replace('published.store(std::make_shared<const Snapshot>(state));','std::atomic_store(&published,std::make_shared<const Snapshot>(state));')
    text=text.replace('published.store(std::make_shared<const InstallView>(state));','std::atomic_store(&published,std::make_shared<const InstallView>(state));')
    text=re.sub(r'std::to_string\(((?:std::filesystem::|fs::)last_write_time\(path\)|(?:entry|e)\.last_write_time\(\))\.time_since_epoch\(\)\.count\(\)\)',r'dkr::release::file_stamp(\1)',text)
    if 'published.load()' in text or 'published.store(' in text:raise ValueError('Unconverted Android shared_ptr operation')
    (a.output/name).write_text(text,encoding='utf-8',newline='\n')
(a.output/'android_store_compat.hpp').write_bytes((auth/'android_store_compat.hpp').read_bytes())
(a.output/'mod_store_android_http.inl').write_bytes((auth/'mod_store_android_http.inl').read_bytes())
(a.output/'SOURCE-RECEIPT.json').write_text(json.dumps({'desktop_input_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),
    'files':{name:hashlib.sha256((a.output/name).read_bytes()).hexdigest() for name in (*sources,'mod_store_android_http.inl','android_store_compat.hpp')},
    'policy':'Android-only platform transport; default TLS verification, explicit redirect/credential policy, bounded streaming; no target execution.'},indent=2)+'\n')

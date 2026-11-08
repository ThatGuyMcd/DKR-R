"""Generate a lean Android packaging command from the reviewed platform recipe."""
from pathlib import Path
import argparse
import re
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
p.add_argument('--products',type=Path,required=True);a=p.parse_args()
if a.output.exists() or a.products.exists():raise ValueError('Choose NEW Android recipe/product paths')
text=(ROOT/'scripts/Package-Android-Preview.ps1').read_text(encoding='utf-8')
text=text.replace('$projectRoot = Split-Path $PSScriptRoot -Parent',"$projectRoot = '"+ROOT.as_posix()+"'")
start=text.index('$codeSdkArguments = ');end=text.index('$libraries = @(',start)
text=text[:start]+text[end:]
# The SDK and guides are separate downloads, never APK runtime assets.
text=text.replace("Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/UPDATES.md') -Destination $notices",'')
text=text.replace("Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.md')", "Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/RELEASE-README.md') -Destination (Join-Path $notices 'README.txt')")
text=text.replace('@versionArguments :app:clean $assembleTask --no-daemon',
    "@versionArguments --init-script '"+(ROOT/'patches/sdk2/android_startup_init.gradle').as_posix()+"' '-DdkrStartupBuildRoot="+a.products.resolve().as_posix()+"' :app:clean $assembleTask --no-daemon")
old='Join-Path $projectRoot "packaging/android/app/build/outputs/apk/$variant/app-$variant.apk"'
assert text.count(old)==1
text=text.replace(old,"Join-Path '"+(a.products.resolve()/'app').as_posix()+"' \"outputs/apk/$variant/app-$variant.apk\"")
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text,encoding='utf-8',newline='\n')
print('Generated lean APK packaging recipe, no application execution:',a.output)

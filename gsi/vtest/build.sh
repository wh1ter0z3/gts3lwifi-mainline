#!/bin/sh
# Builds vtest.apk with the Android SDK (platform 30, build-tools), signed with
# a throwaway key made on the first run.
# usage: SDK=<android-sdk dir> sh build.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
BT=$(ls -d "$SDK"/build-tools/* | tail -1)
JAR=$SDK/platforms/android-30/android.jar
W=$HERE/.build
rm -rf "$W" && mkdir -p "$W/classes" "$W/dex"
javac --release 17 -classpath "$JAR" -d "$W/classes" $(find "$HERE/src" -name '*.java')
"$BT/d8" --lib "$JAR" --min-api 29 --output "$W/dex" $(find "$W/classes" -name '*.class')
"$BT/aapt2" link -o "$W/base.apk" --manifest "$HERE/AndroidManifest.xml" -I "$JAR" \
	--min-sdk-version 29 --target-sdk-version 30
(cd "$W/dex" && zip -q -j "$W/base.apk" classes.dex)
"$BT/zipalign" -f 4 "$W/base.apk" "$W/aligned.apk"
[ -f "$HERE/.key.jks" ] || keytool -genkeypair -keystore "$HERE/.key.jks" -storepass vtestkey \
	-alias vtest -keyalg RSA -keysize 2048 -validity 10000 -dname CN=vtest >/dev/null 2>&1
"$BT/apksigner" sign --ks "$HERE/.key.jks" --ks-pass pass:vtestkey --out "$HERE/vtest.apk" "$W/aligned.apk"
echo "built vtest.apk"

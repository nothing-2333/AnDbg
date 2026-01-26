./build.sh
adb push ./build/bin/AnDbg /data/local/tmp
python test/test.py 
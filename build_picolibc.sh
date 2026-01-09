#!/bin/sh

mkdir -p picolibc/build
cd picolibc/build

echo Path to GCC is `which arm-none-eabi-gcc`

../scripts/do-configure arm-none-eabi \
--reconfigure \
-Dmultilib-list=thumb/v6-m/nofp,thumb/v8-m.main/nofp,thumb/v8-m.main+fp/softfp,thumb/v8-m.main+fp/hard,thumb/v8-m.main+dp/softfp,thumb/v8-m.main+dp/hard \
-Dspecsdir=none \
-Dsysroot-install=true \
-Dposix-console=true \
-Dsemihost=false \
-Dpicolib=true \
-Dpicocrt=false \
-Dthread-local-storage=true \
-Dtls-rp2040=true \
-Dmb-capable=true

ninja

ninja install

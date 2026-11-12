#!/bin/sh
# Ruleaza testele logicii pe calculator (gcc): extrage config + logica direct din ../src/main.c.
set -e
cd "$(dirname "$0")"
SRC=../src/main.c
mkdir -p build
awk '/1\. CONFIGURARE/{f=1;next} /2\. HARDWARE/{f=0} f' $SRC | grep -v '^/\* ====' > build/config_extract.h
sed 's/#define COLOR_BY_THIRDS 0/#define COLOR_BY_THIRDS 1/' build/config_extract.h > build/config_thirds.h
awk '/\/\* LOGIC BEGIN \*\//{f=1;next} /\/\* LOGIC END \*\//{f=0} f' $SRC > build/logic_extract.inc
awk '/uint32_t k = 0;/{f=1} f{print} /pwm_buf\[k\+\+\] = 0;/{if(f){exit}}' $SRC > build/fill_extract.inc

CC=${CC:-gcc}
FLAGS="-std=c11 -O1 -w -Ibuild"
echo "== logica, praguri fixe (implicit) =="
$CC $FLAGS -fsanitize=address,undefined -o build/harness harness.c && build/harness
echo "== logica, treimi din durata =="
$CC $FLAGS -DCONFIG_HEADER='"config_thirds.h"' -fsanitize=address,undefined -o build/harness_thirds harness.c && build/harness_thirds | tail -1
echo "== sirul de biti spre LED-uri =="
$CC -std=c11 -Wall -Wextra -Ibuild -o build/bits_test bits_test.c && build/bits_test

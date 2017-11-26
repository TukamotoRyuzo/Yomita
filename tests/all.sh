#!/bin/bash

#if [ "x$1" != "xsse42" ]; then
if [ "x$1" != "xsse42" || "x$1" != "xavx2" ]; then
  echo "testing cancel(!= $1)"
  exit 0
fi

echo "testing started"
pwd

wget https://github.com/TukamotoRyuzo/Yomita/releases/download/SDT5/yomitaSDT5.zip -O yomitaSDT5.zip
if [ $? != 0 ]; then
  echo "testing failed(wget)"
  exit 1
fi

unzip -o yomitaSDT5.zip
if [ $? != 0 ]; then
  echo "testing failed(unzip)"
  exit 1
fi

mv ./yomita_engine/* ./

echo "loading eval.bin.."
cat eval/kppt/*.bin eval/kppt/*/*.bin > /dev/null 2>&1

./perft.sh
./moves.sh



#!/bin/bash

error()
{
  echo "perft testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "perft testing started"

keys=("nodes" "captures" "promotions" "checks" "checkmates");

# start pos で 1 から 5 まで
for nn in {1..5}; do
  echo "----- perft $nn START_POS -----";
  ./Yomita-by-clang << END_OF_USI > result.txt
isready
position startpos
perft $nn
END_OF_USI
  for str in "${keys[@]}"; do
    grep $str result.txt
  done;
done;

# max で 1 から 3 まで
for nn in {1..3}; do
  echo "----- perft $nn MAX_MOVES_POS -----";
  ./Yomita-by-clang << END_OF_USI > result.txt
max
perft $nn
END_OF_USI
  for str in "${keys[@]}"; do
    grep $str result.txt
  done;
done;

rm result.txt
echo "---"
echo "perft testing OK"


#!/bin/bash

error()
{
  echo "moves testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "moves testing started"

(
  echo "isready";
  echo "usinewgame";
  echo "position startpos moves 7g7f";
  echo "go btime 0 wtime 0 byoyomi 5000";
  sleep 6;
  echo "position startpos moves 7g7f 8c8d 8h7g";
  echo "go btime 0 wtime 0 byoyomi 5000";
  sleep 6;
  echo "position startpos moves 7g7f 8c8d 8h7g 4a3b 3i3h";
  echo "go btime 0 wtime 0 byoyomi 5000";
  sleep 6;
) | ./Yomita-by-clang | tee result.txt


# resignを除くbestmoveの応答が3つ無い場合は失敗
rtn=`grep -v resign result.txt | grep bestmove | wc -l`
if [ "x${rtn}" != "x3" ]; then
  echo "moves testing failed(bestmove?)"
  exit 1
fi

rm result.txt
echo "---"
echo "moves testing OK"


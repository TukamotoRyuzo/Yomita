# 読み太
 [![Build Status](https://travis-ci.org/TukamotoRyuzo/Yomita.svg?branch=master)](https://travis-ci.org/TukamotoRyuzo/Yomita)

読み太はUSIプロトコル準拠の将棋エンジンです。  
主にチェスエンジンのStockfishをベースとし、  
学習部、評価関数のデータ構造はやねうら王を参考にしています。  
また、一部の処理はAperyを参考にしています。  
  
実行ファイル、評価関数バイナリはこちらからダウンロードできます。
https://github.com/TukamotoRyuzo/Yomita/releases/tag/SDT5  

付属している評価関数バイナリは、読み太でのみ使うことができます。  
付属しているbook.txtは局面に対する指し手が1手しか登録されていないので、毎回必ず同じ定跡を選択します。  
定跡を使用したくない場合、エンジン設定で「UseBook」のチェックを外してください。  



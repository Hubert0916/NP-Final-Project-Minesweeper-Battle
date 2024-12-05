# NP Final Project: Minesweeper Battle
---
## 建立與加入房間
![image](https://github.com/user-attachments/assets/b9bde14c-e5a6-419b-a17f-aba72b698a07)
## 遊戲狀態
### 順序
Ex: A	(T)B	(X)C (W)D…. (T:本輪玩家,X:出局,W: 觀戰)
### 地雷數量
Ex: 16
### 地圖
Ex:  
![image](https://github.com/user-attachments/assets/de8a018c-2932-4174-99a3-5337b7620bfc)  
所有人可見: E: 還沒被踩過的方塊 0-8:附近有幾個地雷 B:被踩到的地雷  
僅自己可見: F: 標記地雷 H0-H8:用道具看到的安全格 HB: 用道具看到的地雷
### 道具
Ex:  
A: 透視:1, 跳過回合:2, 偷竊道具:0  
B: 透視:0, 跳過回合:1, 偷竊道具:1  
## 遊戲開始前指令 
### 準備
ready
### 取消
cancel
### 設定地圖
僅host可用  
set {row] {column} {mines}, row*column > num
### 遊戲開始
僅host可用，還沒準備玩家和新玩家設為觀戰  
start
## 輪到玩家時指令
### 偷竊
偷其他玩家道具: steal {Another player’s name} {Item name}
### 透視
偷看還沒被翻開的格子: peek {row} {col}
### 跳過
跳過這回合: pass
### 踩方塊
踩下未知方塊: step {row} {col}
## 隨時都能用的指令
### 離開房間
leave  
如host離開就把host的身分給下一位玩家，  
如房間沒人就釋出房號。
### 指令說明
help
## 做不完時
刪除set功能、刪除道具、刪除準備和取消功能、直接拒絕新加入玩家
## 有餘力
美化、讓host有kick其他玩家指令

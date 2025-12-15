/**
 * TrussC Serial Test for Arduino
 *
 * openFrameworks の serialExample と同じ動作をするテストスケッチ
 */

int ledPin = 13;   // LEDのピン
int val = 0;       // シリアルから読み込んだデータ

void setup() {
    pinMode(ledPin, OUTPUT);    // LED ピンを出力に設定
    Serial.begin(9600);         // シリアルポートに接続
}

void loop() {
    // シリアルポートを読み取り
    val = Serial.read();

    // -1 はデータがない状態
    // 'a' を受信したら "ABC" を返してLEDを点滅
    //
    // 注意: Arduino の TX/RX が安定するまで 5-10 秒かかることがある
    // アップロード後、少し待ってからシリアルモニタを開く
    // 'a' を入力して送信すると、コンソールに ABC が返ってくるはず
    // まずこれを確認してから、TrussC のサンプルを試そう
    //
    // TrussC/oF では、どのシリアルポートで通信するか明示的に指定する必要がある
    // "Tools > Serial Port" でポートを確認
    // 例: "COM7" (Windows), "/dev/tty...." (Mac/Linux)

    if (val != -1) {
        if (val == 'a') {
            Serial.print("ABC");        // シンプルに ABC を返す
            digitalWrite(ledPin, HIGH);
            delay(200);
            digitalWrite(ledPin, LOW);
        }
    }
}

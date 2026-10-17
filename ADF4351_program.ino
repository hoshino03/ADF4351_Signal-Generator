// --- ポート定義 ---
struct Port {
  int CE;
  int LE;
  int SCK;
  int MOSI;
  int ATT_LE;
};

Port ports[2] = {
  {0, 4, 6, 7, 12},  // PORT0
  {1, 5, 6, 7, 13}   // PORT1
};

// --- PLL設定 ---
const uint8_t fpfd = 10;
const uint8_t fpfd_internal = 20;
const uint32_t nmod_base = 20000;

// --- 状態 ---
enum State { WAIT_PORT, WAIT_MODE, WAIT_VALUE };
State state = WAIT_PORT;

int currentPort = 0;
char currentMode = 'F';

String buffer = "";

// --- 状態保持 ---
float currentFreqHz[2] = {0, 0};
float currentAtt[2] = {0, 0};

// SPI送信
void writeSPI(uint32_t data, int port) {
  digitalWrite(ports[port].LE, LOW);

  shiftOut(ports[port].MOSI, ports[port].SCK, MSBFIRST, (data >> 24) & 0xFF);
  shiftOut(ports[port].MOSI, ports[port].SCK, MSBFIRST, (data >> 16) & 0xFF);
  shiftOut(ports[port].MOSI, ports[port].SCK, MSBFIRST, (data >> 8) & 0xFF);
  shiftOut(ports[port].MOSI, ports[port].SCK, MSBFIRST, data & 0xFF);

  digitalWrite(ports[port].LE, HIGH);
}

// 周波数設定
void setFrequency(uint32_t freq_kHz, int port) {

  currentFreqHz[port] = freq_kHz * 1000.0;

  uint32_t registers[6];
  uint8_t i = 0;
  uint16_t nint, nfrac_i, nmodulus, a, b, r;
  uint32_t ndiv = 1;

  float freq_f = freq_kHz / 1000.0;

  float temp = 4400.0 / freq_f;
  while (temp >= 2.0 && i < 6) {
    temp /= 2.0;
    i++;
  }
  ndiv = 1 << i;

  nint = (freq_kHz * ndiv) / (fpfd_internal * 1000);
  temp = (freq_f * ndiv) / fpfd_internal;

  float nfrac_float = nmod_base * (temp - nint);
  nfrac_i = (uint16_t)round(nfrac_float);

  a = nmod_base;
  b = nfrac_i;

  if (b != 0) {
    r = a % b;
    while (r != 0) {
      a = b;
      b = r;
      r = a % b;
    }
    nfrac_i /= a;
    nmodulus = nmod_base / a;
  } else {
    nfrac_i = 0;
    nmodulus = 2;
  }

  if (nmodulus == 1) nmodulus = 2;

  registers[0] = ((uint32_t)nint << 15) | ((uint32_t)nfrac_i << 3);
  registers[1] = (1UL << 27) | (1UL << 15) | ((uint32_t)nmodulus << 3) | 1;
  registers[2] = 0x16005E42;
  registers[3] = 0x000004B3;
  registers[4] = (1UL << 23) | ((uint32_t)i << 20) | (200UL << 12) | (1UL << 5) | (3UL << 3) | 4;
  registers[5] = 0x00580005;

  for (int k = 5; k >= 1; k--) {
    writeSPI(registers[k], port);
    delay(2);
  }
  delay(20);
  writeSPI(registers[0], port);

  Serial.print("OK FREQ ");
  Serial.print(freq_kHz / 1000.0);
  Serial.println(" MHz");
}

// アッテネータ
void setAttenuation(float db, int port) {

  currentAtt[port] = db;

  if (db < 0) db = 0;
  if (db > 31.5) db = 31.5;

  uint8_t val = (uint8_t)(db * 2);
  val = 63 - val;

  digitalWrite(ports[port].ATT_LE, LOW);

  for (int i = 5; i >= 0; i--) {
    digitalWrite(ports[port].SCK, LOW);
    digitalWrite(ports[port].MOSI, (val >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(ports[port].SCK, HIGH);
    delayMicroseconds(1);
  }

  digitalWrite(ports[port].ATT_LE, HIGH);
  delayMicroseconds(2);
  digitalWrite(ports[port].ATT_LE, LOW);

  Serial.print("OK ATT ");
  Serial.print(db);
  Serial.println(" dB");
}

// STATUS表示
void printStatus() {
  Serial.println("------ STATUS ------");

  for (int i = 0; i < 2; i++) {
    Serial.print("PORT");
    Serial.print(i);
    Serial.print(" : ");

    Serial.print(currentFreqHz[i] / 1e6, 6);
    Serial.print(" MHz, ");

    Serial.print(currentAtt[i]);
    Serial.println(" dB");
  }

  Serial.println("--------------------");
}

// 単位パース
float parseNumber(String s) {
  s.trim();
  s.toUpperCase();

  if (s.endsWith("G")) return s.substring(0, s.length() - 1).toFloat() * 1e9;
  if (s.endsWith("M")) return s.substring(0, s.length() - 1).toFloat() * 1e6;
  if (s.endsWith("K")) return s.substring(0, s.length() - 1).toFloat() * 1e3;

  return s.toFloat();
}

// 入力処理
void processInput(String input) {
  input.trim();
  input.toUpperCase();

  // --- STATUS ---
  if (input == "S" || input == "STATUS") {
    printStatus();
    return;
  }

  // --- キャンセル ---
  if (input == "Q") {
    Serial.println("CANCEL");
    state = WAIT_PORT;
    return;
  }

  switch (state) {
    case WAIT_PORT:
      if (input == "0" || input == "1") {
        currentPort = input.toInt();
        state = WAIT_MODE;
      } else {
        Serial.println("ERR PORT");
      }
      break;

    case WAIT_MODE:
      if (input == "F" || input == "A") {
        currentMode = input[0];
        state = WAIT_VALUE;
      } else {
        Serial.println("ERR MODE");
      }
      break;

    case WAIT_VALUE:
      if (currentMode == 'F') {
        float freq = parseNumber(input);
        setFrequency(freq / 1000, currentPort);
      } else {
        float att = input.toFloat();
        setAttenuation(att, currentPort);
      }
      state = WAIT_PORT;
      break;
  }
}

// プロンプト
void prompt() {
  switch (state) {
    case WAIT_PORT:
      Serial.print("PORT? (0/1): ");
      break;
    case WAIT_MODE:
      Serial.print("MODE? (F/A): ");
      break;
    case WAIT_VALUE:
      if (currentMode == 'F') Serial.print("FREQ? ");
      else Serial.print("ATT? ");
      break;
  }
}

// setup
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 2; i++) {
    pinMode(ports[i].CE, OUTPUT);
    pinMode(ports[i].LE, OUTPUT);
    pinMode(ports[i].ATT_LE, OUTPUT);
  }

  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);

  digitalWrite(6, LOW);
  digitalWrite(7, LOW);

  delay(2000);

  digitalWrite(ports[0].CE, HIGH);
  digitalWrite(ports[1].CE, HIGH);

  Serial.println("READY");
  prompt();
}

// loop
void loop() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') continue;

    // --- 確定 ---
    if (c == '\r') {
      Serial.println();

      if (buffer.length() > 0) {
        processInput(buffer);
        buffer = "";
      }

      prompt();
      continue;
    }

    //バックスペース
    if (c == 0x08 || c == 0x7F) {
      if (buffer.length() > 0) {
        buffer.remove(buffer.length() - 1);
        Serial.print("\b \b");
      }
      continue;
    }

    //通常入力
    buffer += c;
    Serial.print(c);
  }
}

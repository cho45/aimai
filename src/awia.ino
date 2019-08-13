#include "awia.h"
#include <SparkFunSi4703.h>
#include <Adafruit_Si4713.h>
#include <esp32-hal-timer.h>
#include <esp_attr.h>
#include <SSD1306.h>

SSD1306 display(OLED_ADDR, SDA_PIN, SCL_PIN, GEOMETRY_128_64);
Si4703_Breakout rx(RX_RST_PIN, SDA_PIN, SCL_PIN, UNUSED);
Adafruit_Si4713 tx(TX_RST_PIN);

volatile int16_t rxVol = 0; // TODO load init value from the nonvolatile memory
volatile int16_t rxFreq = JP_MINIMUM_FM_MHZ; // TODO load init value from the nonvolatile memory
volatile int16_t txFreq = JP_MINIMUM_FM_MHZ; // TODO load init value from the nonvolatile memory

volatile bool rxShouldInit = true;
volatile bool txShouldInit = true;

hw_timer_t *tickRDSReadingTimer = nullptr;
volatile bool shouldRDSRead = false;
char rdsBuff[RDS_TEXT_LENGTH]; // thi param is used by both of the state (i.e. rx and tx)

void (*concreteLoop)();

void IRAM_ATTR tickRDSReading() {
    shouldRDSRead = true;
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("setup...");

    display.init();
    display.setFont(ArialMT_Plain_24);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 16, "AIMAI");
    display.display();

    pinMode(RIGHT_ENC_PIN_A, INPUT_PULLUP);
    pinMode(RIGHT_ENC_PIN_B, INPUT_PULLUP);
    pinMode(RIGHT_ENC_SW_PIN, INPUT_PULLUP);
    pinMode(LEFT_ENC_PIN_A, INPUT_PULLUP);
    pinMode(LEFT_ENC_PIN_B, INPUT_PULLUP);
    pinMode(LEFT_ENC_SW_PIN, INPUT_PULLUP);

    pinMode(RX_MODE_PIN, INPUT_PULLUP);
    detachInterrupt(RX_MODE_PIN);
    attachInterrupt(RX_MODE_PIN, loadActionMode, CHANGE);

    pinMode(TX_MODE_PIN, INPUT_PULLUP);
    detachInterrupt(TX_MODE_PIN);
    attachInterrupt(TX_MODE_PIN, loadActionMode, CHANGE);

    loadActionMode();
}

void initRx() {
    detachInterrupt(RIGHT_ENC_PIN_A);
    attachInterrupt(RIGHT_ENC_PIN_A, changeRxVolume, CHANGE);
    detachInterrupt(RIGHT_ENC_PIN_B);
    attachInterrupt(RIGHT_ENC_PIN_B, changeRxVolume, CHANGE);

    detachInterrupt(LEFT_ENC_PIN_A);
    attachInterrupt(LEFT_ENC_PIN_A, changeRxFreq, CHANGE);
    detachInterrupt(LEFT_ENC_PIN_B);
    attachInterrupt(LEFT_ENC_PIN_B, changeRxFreq, CHANGE);

    rx.powerOn();
    rx.setChannel(0);
    rx.setVolume(0);

    tickRDSReadingTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(tickRDSReadingTimer, &tickRDSReading, true);
    timerAlarmWrite(tickRDSReadingTimer, RDS_READING_PERIOD_MICROS, true);
    timerAlarmEnable(tickRDSReadingTimer);

    rxShouldInit = true;
}

// TODO
char txRDSText[RDS_TEXT_LENGTH] = {'(', 'e', 'm', 'p', 't', 'y', ')', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                   ' ', ' ', ' '};
volatile int txRDSBuffCursor = 0;

void initTx() {
    detachInterrupt(RIGHT_ENC_PIN_A);
    attachInterrupt(RIGHT_ENC_PIN_A, editRDSText, CHANGE);
    detachInterrupt(RIGHT_ENC_PIN_B);
    attachInterrupt(RIGHT_ENC_PIN_B, editRDSText, CHANGE);

    detachInterrupt(RIGHT_ENC_SW_PIN);
    attachInterrupt(RIGHT_ENC_SW_PIN, enterRDSTextCharacter, FALLING);

    detachInterrupt(LEFT_ENC_PIN_A);
    attachInterrupt(LEFT_ENC_PIN_A, changeTxFreq, CHANGE);
    detachInterrupt(LEFT_ENC_PIN_B);
    attachInterrupt(LEFT_ENC_PIN_B, changeTxFreq, CHANGE);
    detachInterrupt(LEFT_ENC_SW_PIN);
    attachInterrupt(LEFT_ENC_SW_PIN, escapeRDSTextEditing, FALLING);

    tx.begin();
    tx.setTXpower(TX_POWER);

    // TODO
    tx.beginRDS();
    tx.setRDSstation(STATION_NAME);
    tx.setRDSbuffer(txRDSText);

    txShouldInit = true;
}

int mainLoopRxFreq = 0;
int mainLoopRxVol = 0;
int mainLoopTxFreq = 0;
volatile bool txRDSTextChanged = false;

void rxLoop() {
    if (rxShouldInit) {
        rxShouldInit = false;

        // TODO read from nonvolatile memory
        rx.setChannel(800);
        rx.setVolume(1);
        rx.readRDS(rdsBuff, RDS_READING_TIMEOUT_MILLIS);
    }

    if (mainLoopRxFreq != rxFreq) {
        rx.setChannel(rxFreq);
        mainLoopRxFreq = rxFreq;
    }

    if (mainLoopRxVol != rxVol) {
        rx.setVolume(rxVol);
        mainLoopRxVol = rxVol;
    }

    if (shouldRDSRead) {
        rx.readRDS(rdsBuff, RDS_READING_TIMEOUT_MILLIS);
        shouldRDSRead = false;
    }
}

void txLoop() {
    if (txShouldInit) {
        txShouldInit = false;

        // TODO read from nonvolatile memory
        txFreq = JP_MINIMUM_FM_MHZ;
        mainLoopTxFreq = txFreq;
        tx.tuneFM(txFreq * 10);
    }

    if (mainLoopTxFreq != txFreq) {
        tx.tuneFM(txFreq * 10);
        mainLoopTxFreq = txFreq;
    }

    if (txRDSTextChanged) {
        tx.setRDSbuffer(txRDSText);
        txRDSTextChanged = false;
    }
}

void nopLoop() {
}

void loop() {
    (*concreteLoop)();
}

volatile byte posForRxVol;

void changeRxVolume() {
    EncCountStatus encStatus = _readEncCountStatus(RIGHT, &posForRxVol, &rxVol);
    if (encStatus.currentCnt != encStatus.previousCnt) {
        if (encStatus.currentCnt <= 0) {
            rxVol = 0;
        } else if (encStatus.currentCnt > 15) {
            rxVol = 15;
        }
        Serial.print("rx vol: ");
        Serial.println(rxVol);
    }
}

volatile byte posForRxFreq;

void changeRxFreq() {
    EncCountStatus encStatus = _readEncCountStatus(LEFT, &posForRxFreq, &rxFreq);
    if (encStatus.currentCnt != encStatus.previousCnt) {
        if (encStatus.currentCnt < JP_MINIMUM_FM_MHZ) {
            rxFreq = JP_MAXIMUM_FM_MHZ;
        } else if (encStatus.currentCnt > JP_MAXIMUM_FM_MHZ) {
            rxFreq = JP_MINIMUM_FM_MHZ;
        }
        double f = rxFreq / 10.0;
        Serial.print("rx freq: ");
        Serial.print(f);
        Serial.print("MHz");
    }
}

volatile byte posForTxFreq;

void changeTxFreq() {
    EncCountStatus encStatus = _readEncCountStatus(LEFT, &posForTxFreq, &txFreq);
    if (encStatus.currentCnt != encStatus.previousCnt) {
        if (encStatus.currentCnt < JP_MINIMUM_FM_MHZ) {
            txFreq = JP_MAXIMUM_FM_MHZ;
        } else if (encStatus.currentCnt > JP_MAXIMUM_FM_MHZ) {
            txFreq = JP_MINIMUM_FM_MHZ;
        }
        double f = txFreq / 10.0;
        Serial.print("tx freq: ");
        Serial.print(f);
        Serial.print("MHz");
    }
}

#define RUNES_NUM 44
char runes[RUNES_NUM] = {
        ' ',
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', ',', '-',
        '_', '@', '(', ')'
};
volatile byte posForTextInput;
volatile int16_t textInputCursor = 0;
volatile bool rdsEditing = false;

void editRDSText() {
    if (!rdsEditing) {
        rdsEditing = true;
        txRDSBuffCursor = 0;
    }

    EncCountStatus encStatus = _readEncCountStatus(RIGHT, &posForTextInput, &textInputCursor);
    if (encStatus.currentCnt != encStatus.previousCnt) {
        if (encStatus.currentCnt < 0) { // alignment
            textInputCursor = RUNES_NUM - 1;
        } else if (encStatus.currentCnt > (RUNES_NUM - 1)) {
            textInputCursor = 0;
        }
        Serial.println(runes[textInputCursor]);
    }
}

void enterRDSTextCharacter() {
    if (!rdsEditing) {
        return;
    }
    Serial.print("char: ");
    Serial.println(runes[textInputCursor]);
    rdsBuff[txRDSBuffCursor++] = runes[textInputCursor];
    if (txRDSBuffCursor >= RDS_TEXT_LENGTH) {
        escapeRDSTextEditing();
        memcpy(txRDSText, rdsBuff, RDS_TEXT_LENGTH);
        Serial.print("RDS Text: ");
        Serial.println(txRDSText);
        txRDSTextChanged = true;
    }
}

void escapeRDSTextEditing() {
    rdsEditing = false;
}

EncCountStatus _readEncCountStatus(EncSide encSide, volatile byte *pos, volatile int16_t *cnt) {
    byte current;
    switch (encSide) {
        case LEFT:
            current = (!digitalRead(LEFT_ENC_PIN_B) << 1) + !digitalRead(LEFT_ENC_PIN_A);
            break;
        case RIGHT:
            current = (!digitalRead(RIGHT_ENC_PIN_B) << 1) + !digitalRead(RIGHT_ENC_PIN_A);
            break;
        default:
            // absolutely unreachable
            Serial.print("[ERROR] unexpected enc side has come: ");
            Serial.println(encSide);
            return {0, 0};
    }

    byte previous = *pos & 0b11;
    byte direction = (*pos & 0b00110000) >> 4;

    if (current == 3) current = 2;
    else if (current == 2) current = 3;

    int16_t previousCnt = *cnt;

    if (current != previous) {
        if (direction == 0) {
            if (current == 0b01 || current == 0b11) {
                direction = current;
            }
        } else {
            if (current == 0b00) {
                if (direction == 0b01 && previous == 0b11) {
                    (*cnt)++;
                } else if (direction == 0b11 && previous == 0b01) {
                    (*cnt)--;
                }
                direction = 0b00;
            }
        }
        *pos = (direction << 4) + (previous << 2) + current;
    }

    EncCountStatus encStatus = {previousCnt, *cnt};
    return encStatus;
}

void inactivateRx() {
    pinMode(RX_RST_PIN, OUTPUT);
    digitalWrite(RX_RST_PIN, HIGH);
    delay(10);
    digitalWrite(RX_RST_PIN, LOW);
    delay(10);
    digitalWrite(RX_RST_PIN, HIGH);

    timerAlarmDisable(tickRDSReadingTimer);
}

void inactivateTx() {
    tx.reset();
}

void loadActionMode() {
    int rx = digitalRead(RX_MODE_PIN);
    int tx = digitalRead(TX_MODE_PIN);
    if (rx == HIGH && tx == LOW) {
        Serial.println("TX mode");
        inactivateRx();
        initTx();
        concreteLoop = txLoop;
        return;
    }

    if (tx == HIGH && rx == LOW) {
        Serial.println("RX mode");
        inactivateTx();
        initRx();
        concreteLoop = rxLoop;
        return;
    }

    // both of the mode pins are 0 or 1, this handles it as NOP mode
    Serial.println("NOP mode");
    inactivateTx();
    inactivateRx();
    concreteLoop = nopLoop;
}

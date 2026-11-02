/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#include <NimBLEDevice.h>
#include "configuration.h"
#include "lora_utils.h"
#include "kiss_utils.h"
#include "ble_utils.h"
#include "display.h"
#include "logger.h"

#define BLE_CHUNK_SIZE  240
#define MAX_KISS_BUFFER 1024


// APPLE - APRS.fi app
#define SERVICE_UUID_0            "00000001-ba2a-46c9-ae49-01b0961f68bb"
#define CHARACTERISTIC_UUID_TX_0  "00000003-ba2a-46c9-ae49-01b0961f68bb"
#define CHARACTERISTIC_UUID_RX_0  "00000002-ba2a-46c9-ae49-01b0961f68bb"

// ANDROID - BLE Terminal app (Serial Bluetooth Terminal from Playstore)
#define SERVICE_UUID_1            "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX_1  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX_1  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer               *pServer            = nullptr;
BLECharacteristic       *pCharacteristicTx  = nullptr;
BLECharacteristic       *pCharacteristicRx  = nullptr;

extern Configuration    Config;
extern logging::Logger  logger;
extern bool             bluetoothConnected;
extern bool             bluetoothActive;

bool    shouldSendBLEtoLoRa     = false;
String  BLEToLoRaPacket         = "";
String  kissSerialBuffer        = "";


class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {      // NimBLE-Arduino 2.x signature
        bluetoothConnected = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "%s", "BLE Client Connected");
        #ifdef RPC_MicroTracker
            digitalWrite(13, LOW);    // Led BT/BLE
        #endif
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        bluetoothConnected = false;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BLE", "%s", "BLE client Disconnected, Started Advertising");
        #ifdef RPC_MicroTracker
            digitalWrite(13, HIGH);    // Led BT/BLE
        #endif
        if (!pServer->getAdvertising()->isAdvertising()) pServer->startAdvertising();      // 2.x already re-advertises by default
    }
};

class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        if (Config.bluetooth.useKISS) {   // KISS (AX.25)
            std::string receivedData = pCharacteristic->getValue();

            for (uint8_t c : receivedData) {                                                // save all received data from buffer
                kissSerialBuffer += (char)c;
            }
            if (kissSerialBuffer.length() > MAX_KISS_BUFFER) {                              // buffer overflow protection
                kissSerialBuffer = "";
                return;
            }

            int maxIterations = 10;                                                         // infinite loop protection
            while (maxIterations-- > 0) {

                if (kissSerialBuffer.length() == 0) break;                                  // empty buffer protection

                int fendIndex = -1;
                if (kissSerialBuffer.charAt(0) == (char)KissChar::FEND) {                   // starts with FEND???
                    for (size_t i = 1; i < kissSerialBuffer.length(); i++) {                // look for next FEND
                        if (kissSerialBuffer.charAt(i) == (char)KissChar::FEND) {
                            fendIndex = (int)i;
                            break;
                        }
                    }
                } else {
                    int firstFendIndex = kissSerialBuffer.indexOf((char)KissChar::FEND);    // find first FEND byte to discard leading corrupted bytes
                    if (firstFendIndex != -1) {
                        kissSerialBuffer.remove(0, firstFendIndex);                         // delete corrupted data before FEND
                    } else {
                        kissSerialBuffer = "";                                              // if no FEND found, delete all
                        break;
                    }
                    continue;
                }

                if (fendIndex == -1) {                                                      // exit: no FEND byte to process the kissSerialBuffer (yet)
                    break;
                }

                String frame = kissSerialBuffer.substring(0, fendIndex + 1);                // extract full frame (With FEND at start and end)
                kissSerialBuffer.remove(0, fendIndex + 1);

                if (frame.length() >= 4) {                                                  // FEND | CMD | DATA | FEND
                    bool isDataFrame    = false;
                    BLEToLoRaPacket     = KISS_Utils::decodeKISS(frame, isDataFrame);
                    if (isDataFrame) shouldSendBLEtoLoRa = true;
                }
            }
        } else {                            // TNC2
            std::string receivedData = pCharacteristic->getValue();
            if (!receivedData.empty()) {
                BLEToLoRaPacket = String(receivedData.c_str());
                BLEToLoRaPacket.trim();
                if (!BLEToLoRaPacket.isEmpty()) shouldSendBLEtoLoRa = true;
            }
        }
    }
};

namespace BLE_Utils {

    void stop() {
        BLEDevice::deinit();
    }

    void setup() {
        String BLEid = Config.bluetooth.deviceName;
        BLEDevice::init(BLEid.c_str());
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        BLEService *pService = nullptr;

        //  KISS (AX.25) or TNC2
        bool useKISS = Config.bluetooth.useKISS;
        pService = pServer->createService(useKISS ? SERVICE_UUID_0 : SERVICE_UUID_1);
        pCharacteristicTx = pService->createCharacteristic(useKISS ? CHARACTERISTIC_UUID_TX_0 : CHARACTERISTIC_UUID_TX_1, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        pCharacteristicRx = pService->createCharacteristic(useKISS ? CHARACTERISTIC_UUID_RX_0 : CHARACTERISTIC_UUID_RX_1, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);

        if (pService != nullptr) {
            pCharacteristicRx->setCallbacks(new MyCallbacks());
            // NimBLE-Arduino 2.x: NimBLEService::start() is deprecated (no effect); services start with advertising

            BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
            pAdvertising->addServiceUUID(useKISS ? SERVICE_UUID_0 : SERVICE_UUID_1);

            pServer->getAdvertising()->enableScanResponse(true);
            pServer->getAdvertising()->setPreferredParams(0x06, 0x0C);
            pAdvertising->start();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE", "%s", "Waiting for BLE central to connect...");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "BLE", "Failed to create BLE service");
        }
    }

    void sendToLoRa() {
        if (!shouldSendBLEtoLoRa) return;

        logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE Tx", "%s", BLEToLoRaPacket.c_str());
        displayShow("BLE Tx >>", "", BLEToLoRaPacket, 1000);
        LoRa_Utils::sendNewPacket(BLEToLoRaPacket);
        BLEToLoRaPacket = "";
        shouldSendBLEtoLoRa = false;
    }

    void txToPhoneOverBLE(const String& frame) {
        if (pCharacteristicTx == nullptr) return;
        const String payload = Config.bluetooth.useKISS ? KISS_Utils::encodeKISS(frame) : (frame + "\n");
        const uint8_t* data  = (const uint8_t*)payload.c_str();
        size_t length        = payload.length();

        for (size_t i = 0; i < length; i += BLE_CHUNK_SIZE) {
            size_t chunkSize = (length - i < BLE_CHUNK_SIZE) ? (length - i) : BLE_CHUNK_SIZE;
            pCharacteristicTx->setValue(data + i, chunkSize);
            pCharacteristicTx->notify();
            if (i + BLE_CHUNK_SIZE < length) delay(20);
        }
    }

    void sendToPhone(const String& packet) {
        if (!packet.isEmpty() && bluetoothConnected) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "BLE Rx", "%s", packet.c_str());
            txToPhoneOverBLE(packet);
        }
    }

}
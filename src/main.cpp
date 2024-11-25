#include <M5Core2.h>
#include <AudioFileSource.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "FS.h"
#include "SD.h"
#include <esp_ota_ops.h>
#include <nvs_flash.h>

// Launcher definitions
#define LAUNCHER_MAGIC_NUMBER 0xA5A5A5A5
#define NVS_NAMESPACE "launcher"
#define NVS_BOOT_KEY "boot_flag"

// Audio objects
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;

// UI constants
#define DISPLAY_FILES 5
#define FILE_BUTTON_HEIGHT 40
#define CONTROL_BUTTON_HEIGHT 50
#define SCROLL_BUTTON_WIDTH 50
#define LIST_START_Y 60

void setupNVS();
void listMP3Files(const char* dirPath);
void startPlaying();
void stopPlaying();
void returnToLauncher();
void drawUI();
void drawFileList();
void handleTouch();
void setup();
void loop();

// Global variables
String mp3files[50];
int fileCount = 0;
int currentFile = 0;
int displayStartIndex = 0;
bool isPlaying = false;
int volume = 50;
unsigned long pressStartTime = 0;

void setupNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void listMP3Files(const char* dirPath) {
    File root = SD.open(dirPath);
    if (!root) {
        return;
    }
    
    File file = root.openNextFile();
    while (file && fileCount < 50) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
                mp3files[fileCount++] = filename;
            }
        }
        file = root.openNextFile();
    }
}

void startPlaying() {
    if (!mp3files[currentFile].startsWith("/")) {
        mp3files[currentFile] = "/" + mp3files[currentFile];
    }
    
    if (file) delete file;
    file = new AudioFileSourceSD(mp3files[currentFile].c_str());
    
    if (!mp3->begin(file, out)) {
        Serial.println("Failed to start MP3");
        return;
    }
    isPlaying = true;
    drawUI();
}

void stopPlaying() {
    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    if (file) {
        file->close();
        delete file;
        file = nullptr;
    }
}

void returnToLauncher() {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    
    if (factory) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_set_u32(handle, NVS_BOOT_KEY, LAUNCHER_MAGIC_NUMBER);
            nvs_commit(handle);
            nvs_close(handle);
        }

        const esp_partition_t* otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
        if (otadata != NULL) {
            esp_partition_erase_range(otadata, 0, otadata->size);
        }

        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.setTextSize(2);
        M5.Lcd.setCursor(20, 100);
        M5.Lcd.print("Returning to launcher...");
        delay(1000);
        
        esp_ota_set_boot_partition(factory);
        ESP.restart();
    }
}

void drawUI() {
    // Clear screen
    M5.Lcd.fillScreen(TFT_BLACK);
    
    // Draw title bar
    M5.Lcd.fillRect(0, 0, M5.Lcd.width(), 40, TFT_NAVY);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.drawString("MP3 Player", 10, 10, 2);
    
    // Draw volume
    String volStr = "Vol: " + String(volume) + "%";
    M5.Lcd.drawString(volStr, M5.Lcd.width() - 120, 10, 2);
    
    // Draw file list
    drawFileList();
    
    // Draw control panel background
    M5.Lcd.fillRect(0, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 10, M5.Lcd.width(), CONTROL_BUTTON_HEIGHT + 10, TFT_NAVY);
    
    // Draw control buttons
    int buttonWidth = (M5.Lcd.width() - 20) / 3;
    M5.Lcd.fillRoundRect(10, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 5, buttonWidth - 10, CONTROL_BUTTON_HEIGHT - 5, 8, TFT_BLUE);
    M5.Lcd.fillRoundRect(buttonWidth + 10, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 5, buttonWidth - 10, CONTROL_BUTTON_HEIGHT - 5, 8, isPlaying ? TFT_RED : TFT_GREEN);
    M5.Lcd.fillRoundRect(2 * buttonWidth + 10, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 5, buttonWidth - 10, CONTROL_BUTTON_HEIGHT - 5, 8, TFT_BLUE);
    
    // Draw button labels
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.drawCentreString("PREV", buttonWidth/2, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT + 10, 2);
    M5.Lcd.drawCentreString(isPlaying ? "STOP" : "PLAY", buttonWidth + buttonWidth/2, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT + 10, 2);
    M5.Lcd.drawCentreString("NEXT", 2 * buttonWidth + buttonWidth/2, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT + 10, 2);
}

void drawFileList() {
    // Draw file list area
    M5.Lcd.fillRect(0, 40, M5.Lcd.width(), M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 50, TFT_BLACK);
    
    // Draw scroll buttons if needed
    if (displayStartIndex > 0) {
        M5.Lcd.fillTriangle(M5.Lcd.width() - 30, 50,
                           M5.Lcd.width() - 15, 35,
                           M5.Lcd.width() - 45, 35,
                           TFT_BLUE);
    }
    if (displayStartIndex + DISPLAY_FILES < fileCount) {
        M5.Lcd.fillTriangle(M5.Lcd.width() - 30, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 60,
                           M5.Lcd.width() - 15, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 45,
                           M5.Lcd.width() - 45, M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 45,
                           TFT_BLUE);
    }
    
    // Draw files
    for (int i = 0; i < DISPLAY_FILES && (i + displayStartIndex) < fileCount; i++) {
        int y = LIST_START_Y + i * FILE_BUTTON_HEIGHT;
        
        // Highlight current file
        if (i + displayStartIndex == currentFile) {
            M5.Lcd.fillRoundRect(5, y, M5.Lcd.width() - 60, FILE_BUTTON_HEIGHT - 5, 8, isPlaying ? TFT_DARKGREEN : TFT_NAVY);
        }
        
        // Draw filename
        M5.Lcd.setTextColor(TFT_WHITE);
        String filename = mp3files[i + displayStartIndex];
        int lastSlash = filename.lastIndexOf('/');
        if (lastSlash >= 0) {
            filename = filename.substring(lastSlash + 1);
        }
        if (filename.length() > 25) {
            filename = filename.substring(0, 22) + "...";
        }
        M5.Lcd.drawString(filename, 15, y + 10, 2);
    }
}

void handleTouch() {
    if (M5.Touch.ispressed()) {
        Point p = M5.Touch.getPressPoint();
        
        if (pressStartTime == 0) {
            // Check if touch is in the file list area
            if (p.y >= LIST_START_Y && p.y < M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 10) {
                int fileIndex = (p.y - LIST_START_Y) / FILE_BUTTON_HEIGHT + displayStartIndex;
                if (fileIndex >= 0 && fileIndex < fileCount) {
                    currentFile = fileIndex;
                    stopPlaying();
                    startPlaying();
                }
            }
            // Check if touch is in the control panel
            else if (p.y >= M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 5) {
                int buttonWidth = (M5.Lcd.width() - 20) / 3;
                
                // Previous button
                if (p.x < buttonWidth) {
                    if (currentFile > 0) {
                        currentFile--;
                        stopPlaying();
                        startPlaying();
                    }
                }
                // Play/Stop button
                else if (p.x >= buttonWidth && p.x < 2 * buttonWidth) {
                    if (isPlaying) {
                        stopPlaying();
                        isPlaying = false;
                    } else {
                        startPlaying();
                    }
                    drawUI();
                }
                // Next button
                else if (p.x >= 2 * buttonWidth) {
                    if (currentFile < fileCount - 1) {
                        currentFile++;
                        stopPlaying();
                        startPlaying();
                    }
                }
            }
            // Check scroll buttons
            else if (p.x >= M5.Lcd.width() - 50) {
                if (p.y < 60 && displayStartIndex > 0) {
                    displayStartIndex--;
                    drawFileList();
                }
                else if (p.y > M5.Lcd.height() - CONTROL_BUTTON_HEIGHT - 60 && 
                         displayStartIndex + DISPLAY_FILES < fileCount) {
                    displayStartIndex++;
                    drawFileList();
                }
            }
        }
    }
}

void setup() {
    M5.begin();
    setupNVS();
    
    // Initialize SD card
    if (!SD.begin()) {
        M5.Lcd.println("SD Card Mount Failed");
        return;
    }

    // Initialize audio
    out = new AudioOutputI2S();
    out->SetPinout(12, 0, 2);
    out->SetOutputModeMono(true);
    out->SetGain(((float)volume)/100.0);
    mp3 = new AudioGeneratorMP3();

    // Scan for MP3 files
    listMP3Files("/");
    
    // Initial UI draw
    M5.Lcd.fillScreen(TFT_BLACK);
    drawUI();
}

void loop() {
    M5.update();
    
    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            stopPlaying();
            // Auto-advance to next track
            if (currentFile < fileCount - 1) {
                currentFile++;
                startPlaying();
            } else {
                isPlaying = false;
                drawUI();
            }
        }
    }
    
    handleTouch();
    
    // Check for long press to return to launcher
    if (M5.Touch.ispressed()) {
        if (pressStartTime == 0) {
            pressStartTime = millis();
        }
        else if (millis() - pressStartTime > 5000) {
            stopPlaying();
            returnToLauncher();
        }
    }
    else {
        pressStartTime = 0;
    }
}
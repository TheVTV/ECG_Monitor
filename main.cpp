#define NOGDI
#define NOUSER
#define NOMMIDS
#define NOMMIDSOUND
#include <windows.h>
#undef DrawText
#undef CloseWindow
#undef ShowCursor
#undef PlaySound
#include <raylib.h>
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <map>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

// Sync
std::mutex ecgMutex;

// Consts
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const char* WINDOW_TITLE = "ECG Monitor";
const int MAX_DATA_POINTS = 500;
const int COM_PORT = 11; // CHANGE THIS TO YOUR COM PORT
const int BAUD_RATE = 115200;
const float RECONNECT_INTERVAL = 10.0f;

// Dark/Light mode
Color bgLight = RAYWHITE;
Color bgDark = {30, 30, 30, 255};
Color textLight = DARKGRAY;
Color textDark = RAYWHITE;

// States
enum AppState {
    MENU,
    CONNECTING,
    MEASUREMENT,
    INSTRUCTION,
    HISTORY
};
AppState appState = MENU;
bool darkMode = true;

// Serial connection
HANDLE hSerial;
bool serialConnected = false;
float reconnectTimer = 0.0f;
std::string connectMsg = "";
std::atomic<bool> keepReading{true};
std::thread serialThread;
bool threadStarted = false;

// ECG controls
std::vector<float> ecgData;
std::string electrodeRedStatus = "CHECK";
std::string electrodeYellowStatus = "CHECK";
bool electrodesLocked = false;

// Patients data
static char name[32] = "";
static char surname[32] = "";
static char age[8] = "";
static int gender = 0; // 0 - Male, 1 - Female
static int activeTextBox = 0; // 0: żadne, 1: name, 2: surname, 3: age
static bool isRecording = false;
static std::ofstream recordFile;
static std::string recordFileName;
static std::vector<float> recordBuffer;

// ECG graph scaling
static float scaleX = 1.0f;
static float scaleY = 1.0f;

// History
std::vector<std::string> historyFiles;
int historyScroll = 0;
int selectedHistoryIdx = -1;
std::vector<float> historyEcgData;
std::string historyName, historySurname, historyAge, historyGender, historyDate;
float historyScaleX = 1.0f, historyScaleY = 1.0f;
int historyPlotOffset = 0;

// Function to initialize serial connection
bool initSerialConnection() {
    std::string portName = "\\\\.\\COM" + std::to_string(COM_PORT);

    hSerial = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening COM port" << std::endl;
        return false;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error getting port parameters" << std::endl;
        return false;
    }

    dcbSerialParams.BaudRate = BAUD_RATE;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error setting port parameters" << std::endl;
        return false;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Error setting port timeout" << std::endl;
        return false;
    }

    return true;
}

// Function to read data from serial port
void readSerialData() {
    char buffer[256];
    DWORD bytesRead;

    if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string dataStr(buffer);

            size_t pos = 0;
            while ((pos = dataStr.find('\n')) != std::string::npos) {
                std::string valueStr = dataStr.substr(0, pos);
                dataStr.erase(0, pos + 1);

                try {
                    float value = std::stof(valueStr);
                    if (!electrodesLocked) {
                        if (value == -3) {
                            electrodeRedStatus = "CHECK";
                            electrodeYellowStatus = "CHECK";
                        } else if (value == -2) {
                            electrodeRedStatus = "CHECK";
                            electrodeYellowStatus = "OK";
                        } else if (value == 0) {
                            electrodeRedStatus = "OK";
                            electrodeYellowStatus = "OK";
                            electrodesLocked = true;
                        }
                    }
                    else {
                        std::lock_guard<std::mutex> lock(ecgMutex);
                        if (value < 1000) {
                            ecgData.push_back(ecgData.size() > 0 ? ecgData.back() : value);
                        }
                        else ecgData.push_back(value);

                        if (isRecording) {
                            if (value < 1000) {
                                recordBuffer.push_back(ecgData.size() > 0 ? ecgData.back() : value);
                            }
                            else recordBuffer.push_back(value);
                        }

                        if (ecgData.size() > MAX_DATA_POINTS) {
                            ecgData.erase(ecgData.begin());
                        }
                    }

                } catch (...) {
                    // Ignore errors
                }
            }
        }
    }
}

// Serial thread function
void serialThreadFunc() {
    while (keepReading) {
        readSerialData();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Function to replace Polish characters with Latin equivalents
std::string removePolishChars(const std::string& input) {
    std::string out;
    std::map<std::string, char> polMap = {
        {"ą", 'a'}, {"ć", 'c'}, {"ę", 'e'}, {"ł", 'l'}, {"ń", 'n'},
        {"ó", 'o'}, {"ś", 's'}, {"ż", 'z'}, {"ź", 'z'},
        {"Ą", 'A'}, {"Ć", 'C'}, {"Ę", 'E'}, {"Ł", 'L'}, {"Ń", 'N'},
        {"Ó", 'O'}, {"Ś", 'S'}, {"Ż", 'Z'}, {"Ź", 'Z'}
    };
    for (size_t i = 0; i < input.size(); ) {
        bool replaced = false;
        for (const auto& [pl, latin] : polMap) {
            if (input.compare(i, pl.size(), pl) == 0) {
                out += latin;
                i += pl.size();
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out += input[i];
            ++i;
        }
    }
    return out;
}

// Function to load files from recordings directory
void loadHistoryFiles() {
    historyFiles.clear();
    for (const auto& entry : std::filesystem::directory_iterator("recordings")) {
        if (entry.path().extension() == ".txt")
            historyFiles.push_back(entry.path().string());
    }
    std::sort(historyFiles.begin(), historyFiles.end(), std::greater<>());
}

// Function to load data from file
bool loadHistoryRecord(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    std::getline(file, historyName);
    std::getline(file, historySurname);
    std::getline(file, historyAge);
    std::getline(file, historyGender);
    std::getline(file, historyDate);
    historyEcgData.clear();
    std::string line;
    while (std::getline(file, line)) {
        try { historyEcgData.push_back(std::stof(line)); }
        catch (...) {}
    }
    file.close();
    historyScaleX = 1.0f;
    historyScaleY = 1.0f;
    historyPlotOffset = 0;
    return true;
}

// Function to format date from raw string
std::string formatDate(const std::string& raw) {
    if (raw.size() < 15) return raw;
    // DDMMYYYY_HHMMSS
    std::string day = raw.substr(0, 2);
    std::string month = raw.substr(2, 2);
    std::string year = raw.substr(4, 4);
    std::string hour = raw.substr(9, 2);
    std::string min = raw.substr(11, 2);
    std::string sec = raw.substr(13, 2);
    return day + "." + month + "." + year + ", " + hour + ":" + min + ":" + sec;
}

// Function to safely close the serial connection
void closeSerialConnection() {
    if (serialConnected) {
        CloseHandle(hSerial);
        serialConnected = false;
    }
}

int main() {
    // Init
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(60);
    Texture2D humanImgDark = LoadTexture("assets/human_black.png");
    Texture2D humanImg = LoadTexture("assets/human_white.png");
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);

    // Main loop
    while (!WindowShouldClose()) {
        BeginDrawing();
        Color bg = darkMode ? bgDark : bgLight;
        Color text = darkMode ? textDark : textLight;
        ClearBackground(bg);

        if (appState == MENU) {
            int btnWidth = 300, btnHeight = 60;
            int x = WINDOW_WIDTH / 2 - btnWidth / 2;
            int y = 180;
            electrodeRedStatus = "CHECK";
            electrodeYellowStatus = "CHECK";
            electrodesLocked = false;

            // Buttons
            if (GuiButton(Rectangle{WINDOW_WIDTH - 180, 20, 160, 40}, darkMode ? "Light Mode" : "Dark Mode")) {
                darkMode = !darkMode;
            }
            if (GuiButton(Rectangle{(float)x, (float)y, (float)btnWidth, (float)btnHeight}, "Measure")) {
                appState = CONNECTING;
                reconnectTimer = 0.0f;
                serialConnected = false;
                connectMsg = "Connecting with ECG device...";
            }
            if (GuiButton(Rectangle{(float)x, (float)y + 80, (float)btnWidth, (float)btnHeight}, "History")) {
                appState = HISTORY;
            }
            if (GuiButton(Rectangle{(float)x, (float)y + 160, (float)btnWidth, (float)btnHeight}, "Exit")) {
                break;
            }
            DrawText("ECG Monitor", WINDOW_WIDTH / 2 - MeasureText("ECG Monitor", 40) / 2, 80, 40, text);
        }
        else if (appState == CONNECTING) {
            // Buttons
            if (serialConnected) {
                if (GuiButton(Rectangle{WINDOW_WIDTH / 2 - 70, WINDOW_HEIGHT / 2 + 30, 140, 40}, "Continue")) {
                    appState = INSTRUCTION;
                }
            }

            if (GuiButton(Rectangle{WINDOW_WIDTH / 2 - 70, WINDOW_HEIGHT / 2 + 90, 140, 40}, "Back to Menu")) {
                closeSerialConnection();
                appState = MENU;
            }

            // Connecting
            if (!serialConnected) {
                reconnectTimer -= GetFrameTime();
                if (reconnectTimer <= 0.0f) {
                    if (initSerialConnection()) {
                        serialConnected = true;
                        connectMsg = "Connected with ECG device!";
                    } else {
                        reconnectTimer = RECONNECT_INTERVAL;
                        connectMsg = "Couldn't connect to ECG device, trying again in " + std::to_string((int)reconnectTimer) + " s";
                    }
                } else {
                    connectMsg = "Couldn't connect to ECG device, trying again in " + std::to_string((int)reconnectTimer) + " s";
                }
            }

            DrawText(connectMsg.c_str(), WINDOW_WIDTH / 2 - MeasureText(connectMsg.c_str(), 24) / 2, WINDOW_HEIGHT / 2 - 30, 24, serialConnected ? DARKGREEN : RED);
        }
        else if (appState == INSTRUCTION) {
            // Start reading from serial if not already started
            if (!threadStarted) {
                keepReading = true;
                serialThread = std::thread(serialThreadFunc);
                threadStarted = true;
            }

            // UI
            DrawTextureEx(darkMode == true ? humanImg : humanImgDark, Vector2{20, 10}, 0.0f, 0.42f, WHITE);
            DrawText("INSTRUCTIONS", 350, 20, 40, text);

            const char* msg = "1. Attach three electrodes to your body. Place one on each\nwrist and one on the inside of your right leg near the ankle\n(see the diagram).\n"
                                     "2. Connect the wire with the green tip to the electrode on\nyour leg.\n"
                                     "3. Connect the wire with the yellow tip to the electrode on\nyour left wrist. Check below if the status of the yellow\nelectrode changes to OK. You can also monitor this on\nthe device.\n"
                                     "4. Connect the wire with the red tip to the electrode on\nyour right wrist. Check below if the status of the red\nelectrode changes to OK. You can also monitor this on the\ndevice.\n"
                                     "5. Sit on a chair or lie down during the measurement.\nDo not move or talk, as it may affect the reading quality.\n"
                                     "6. If all the steps above have been completed and all\nelectrode statuses are OK, press \"Measure\".";
            DrawText(msg, 250, 70, 19, text);

            DrawText("ELECTRODE STATUS", 380, 440, 22, text);
            DrawText("YELLOW:", 300, 470, 21, text);
            DrawText("RED:", 550, 470, 21, text);


            DrawText(electrodeYellowStatus.c_str(), 400, 470, 21, electrodeYellowStatus == "OK" ? DARKGREEN : RED);
            DrawText(electrodeRedStatus.c_str(), 610, 470, 21, electrodeRedStatus == "OK" ? DARKGREEN : RED);


            // Buttons
            if (GuiButton(Rectangle{WINDOW_WIDTH - 270, WINDOW_HEIGHT - 80, 180, 50}, "Measure")) {
                appState = MEASUREMENT;
            }

            if (GuiButton(Rectangle{WINDOW_WIDTH - 500, WINDOW_HEIGHT - 80, 180, 50}, "Back to Menu")) {
                closeSerialConnection();
                appState = MENU;
            }
        }
        else if (appState == MEASUREMENT) {
            SetWindowSize(1200, 800);

            // UI
            DrawRectangle(10, 10, 280, 780, Fade(GRAY, 0.15f));
            DrawText("Patient Data", 40, 30, 28, text);

            if (GuiTextBox(Rectangle{40, 80, 200, 40}, name, 32, activeTextBox == 1)) {
                activeTextBox = 1;
            }
            DrawText("Name", 40, 65, 18, text);

            if (GuiTextBox(Rectangle{40, 150, 200, 40}, surname, 32, activeTextBox == 2)) {
                activeTextBox = 2;
            }
            DrawText("Surname", 40, 135, 18, text);

            if (GuiTextBox(Rectangle{40, 220, 50, 40}, age, 8, activeTextBox == 3)) {
                activeTextBox = 3;
            }
            DrawText("Age", 40, 205, 18, text);

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                if (!CheckCollisionPointRec(mouse, Rectangle{40, 80, 200, 40}) &&
                    !CheckCollisionPointRec(mouse, Rectangle{40, 150, 200, 40}) &&
                    !CheckCollisionPointRec(mouse, Rectangle{40, 220, 90, 40})) {
                    activeTextBox = 0;
                    }
            }

            if (GuiButton(Rectangle{110, 220, 100, 40}, gender == 0 ? "Male" : "Female")) {
                if (gender == 0) gender = 1;
                else gender = 0;
            }

            DrawText("Gender", 110, 205, 18, text);

            DrawText("Scale", 40, 290, 22, text);
            if (GuiButton(Rectangle{80, 330, 40, 40}, "#121#")) scaleY *= 1.1f; // Góra
            if (GuiButton(Rectangle{80, 380, 40, 40}, "#120#")) scaleY /= 1.1f; // Dół
            if (GuiButton(Rectangle{30, 355, 40, 40}, "#118#")) scaleX /= 1.1f; // Lewo
            if (GuiButton(Rectangle{130, 355, 40, 40}, "#119#")) scaleX *= 1.1f; // Prawo

            bool fieldsFilled = strlen(name) > 0 && strlen(surname) > 0 && strlen(age) > 0;
            if (!isRecording) {
                if (GuiButton(Rectangle{40, 450, 200, 50}, "Record")) {
                    if (fieldsFilled) {
                        std::filesystem::create_directory("recordings");

                        time_t now = time(0);
                        tm* ltm = localtime(&now);
                        char datetime[32];
                        strftime(datetime, sizeof(datetime), "%d%m%Y_%H%M%S", ltm);
                        std::string safeName = removePolishChars(name);
                        std::string safeSurname = removePolishChars(surname);
                        recordFileName = "recordings/" + safeName + "_" + safeSurname + "_" + datetime + ".txt";

                        recordFile.open(recordFileName);
                        if (recordFile.is_open()) {
                            recordFile << name << "\n";
                            recordFile << surname << "\n";
                            recordFile << age << "\n";
                            recordFile << gender << "\n";
                            recordFile << datetime << "\n";
                            isRecording = true;
                            recordBuffer.clear();
                        }
                    }
                }
            } else {
                if (GuiButton(Rectangle{40, 450, 200, 50}, "Stop")) {
                    for (float v : recordBuffer) {
                        recordFile << v << "\n";
                    }
                    recordFile.close();
                    isRecording = false;
                }
            }

            std::vector<float> ecgCopy;
            {
                std::lock_guard<std::mutex> lock(ecgMutex);
                ecgCopy = ecgData;
            }
            if (!ecgCopy.empty()) {
                float maxVal = *std::max_element(ecgCopy.begin(), ecgCopy.end());
                float minVal = *std::min_element(ecgCopy.begin(), ecgCopy.end());
                float range = maxVal - minVal;
                if (range < 1.0f) range = 1.0f;

                float plotWidth = 850 * scaleX;
                float plotHeight = 650 * scaleY;
                float scaleYval = plotHeight / range;
                float offsetY = 400 + ((maxVal + minVal) / 2) * scaleYval;

                int visiblePoints = (int)(MAX_DATA_POINTS / scaleX);
                if (visiblePoints < 2) visiblePoints = 2;
                if ((int)ecgCopy.size() < visiblePoints) visiblePoints = (int)ecgCopy.size();
                int startIdx = (int)ecgCopy.size() - visiblePoints;

                DrawLine(320, 400, 320 + plotWidth, 400, GRAY);

                for (int i = 1; i < visiblePoints; i++) {
                    float x1 = 320 + (i - 1) * (plotWidth / (float)(visiblePoints - 1));
                    float y1 = offsetY - ecgCopy[startIdx + i - 1] * scaleYval;
                    float x2 = 320 + i * (plotWidth / (float)(visiblePoints - 1));
                    float y2 = offsetY - ecgCopy[startIdx + i] * scaleYval;

                    DrawLine(x1, y1, x2, y2, RED);
                }

                DrawText(TextFormat("ECG Signal - %d points", (int)ecgCopy.size()), 320, 40, 26, text);
                DrawText(TextFormat("Min: %.2f, Max: %.2f", minVal, maxVal), 320, 80, 26, text);
            } else {
                DrawText("Waiting for ECG data...", 600, 400, 26, text);
            }

            if (GuiButton(Rectangle{1040, 30, 140, 50}, "Back to Menu")) {
                closeSerialConnection();
                SetWindowSize(800, 600);
                appState = MENU;
            }
        }
        else if (appState == HISTORY) {
            if (selectedHistoryIdx == -1) {
                static bool loaded = false;
                if (!loaded) { loadHistoryFiles(); loaded = true; }

                DrawText("History", 100, 40, 36, text);

                int listHeight = 600;
                int itemHeight = 50;
                int visibleItems = listHeight / itemHeight;
                int totalItems = (int)historyFiles.size();

                int wheel = GetMouseWheelMove();
                if (wheel != 0) {
                    historyScroll -= wheel;
                    if (historyScroll < 0) historyScroll = 0;
                    if (historyScroll > totalItems - visibleItems) historyScroll = std::max(0, totalItems - visibleItems);
                }

                for (int i = 0; i < visibleItems && (i + historyScroll) < totalItems; ++i) {
                    int idx = i + historyScroll;
                    std::string fname = std::filesystem::path(historyFiles[idx]).filename().string();
                    Rectangle rect = {100.0f, (float)(100 + i * itemHeight), 600, (float)(itemHeight - 5)};
                    if (GuiButton(rect, fname.c_str())) {
                        if (loadHistoryRecord(historyFiles[idx])) {
                            selectedHistoryIdx = idx;
                        }
                    }
                }

                if (GuiButton(Rectangle{WINDOW_WIDTH - 180, 20, 160, 40}, "Back to Menu")) {
                    appState = MENU;
                    loaded = false;
                    historyScroll = 0;
                    selectedHistoryIdx = -1;
                }
            } else {
                SetWindowSize(1200, 800);
                DrawRectangle(10, 10, 280, 780, Fade(GRAY, 0.15f));
                DrawText("Patient Data", 40, 30, 28, text);
                DrawText(("Name: " + historyName).c_str(), 40, 80, 22, text);
                DrawText(("Surname: " + historySurname).c_str(), 40, 120, 22, text);
                DrawText(("Age: " + historyAge).c_str(), 40, 160, 22, text);
                DrawText((std::string("Gender: ") + (historyGender == "0" ? "Male" : "Female")).c_str(), 40, 200, 22, text);
                DrawText("Date: ", 40, 240, 22, text);
                DrawText((formatDate(historyDate)).c_str(), 40, 260, 22, text);

                DrawText("Scale", 40, 290, 22, text);
                if (GuiButton(Rectangle{80, 330, 40, 40}, "#121#")) historyScaleY *= 1.1f;
                if (GuiButton(Rectangle{80, 380, 40, 40}, "#120#")) historyScaleY /= 1.1f;
                if (GuiButton(Rectangle{30, 355, 40, 40}, "#118#")) historyScaleX /= 1.1f;
                if (GuiButton(Rectangle{130, 355, 40, 40}, "#119#")) historyScaleX *= 1.1f;

                int plotVisible = (int)(MAX_DATA_POINTS / historyScaleX);
                if (plotVisible < 2) plotVisible = 2;
                if ((int)historyEcgData.size() < plotVisible) plotVisible = (int)historyEcgData.size();
                int maxOffset = std::max(0, (int)historyEcgData.size() - plotVisible);
                int wheel = GetMouseWheelMove();
                if (wheel != 0) {
                    historyPlotOffset -= wheel * 10;
                    if (historyPlotOffset < 0) historyPlotOffset = 0;
                    if (historyPlotOffset > maxOffset) historyPlotOffset = maxOffset;
                }

                if (!historyEcgData.empty()) {
                    float maxVal = *std::max_element(historyEcgData.begin(), historyEcgData.end());
                    float minVal = *std::min_element(historyEcgData.begin(), historyEcgData.end());
                    float range = maxVal - minVal;
                    if (range < 1.0f) range = 1.0f;

                    float plotWidth = 850 * historyScaleX;
                    float plotHeight = 650 * historyScaleY;
                    float scaleYval = plotHeight / range;
                    float offsetY = 400 + ((maxVal + minVal) / 2) * scaleYval;

                    int startIdx = historyPlotOffset;
                    int endIdx = std::min(startIdx + plotVisible, (int)historyEcgData.size());

                    DrawLine(320, 400, 320 + plotWidth, 400, GRAY);

                    for (int i = startIdx + 1; i < endIdx; i++) {
                        float x1 = 320 + (i - 1 - startIdx) * (plotWidth / (float)(plotVisible - 1));
                        float y1 = offsetY - historyEcgData[i - 1] * scaleYval;
                        float x2 = 320 + (i - startIdx) * (plotWidth / (float)(plotVisible - 1));
                        float y2 = offsetY - historyEcgData[i] * scaleYval;
                        DrawLine(x1, y1, x2, y2, RED);
                    }
                    DrawText(TextFormat("ECG Signal - %d points", (int)historyEcgData.size()), 320, 40, 26, text);
                    DrawText(TextFormat("Min: %.2f, Max: %.2f", minVal, maxVal), 320, 80, 26, text);
                } else {
                    DrawText("No ECG data in file.", 600, 400, 26, text);
                }

                if (GuiButton(Rectangle{1000, 30, 180, 50}, "Back to History")) {
                    selectedHistoryIdx = -1;
                    SetWindowSize(800, 600);
                }
            }
        }

        EndDrawing();
    }

    // Cleanup
    keepReading = false;
    if (threadStarted && serialThread.joinable()) {
        serialThread.join();
    }
    closeSerialConnection();
    CloseWindow();
    UnloadTexture(humanImgDark);
    UnloadTexture(humanImg);
    return 0;
}
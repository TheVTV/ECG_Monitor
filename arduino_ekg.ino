#include <BluetoothSerial.h>
#include <TFT_eSPI.h>
#include <SPI.h>

BluetoothSerial SerialBT;
TFT_eSPI tft = TFT_eSPI();

// Konfiguracja detekcji BPM
const int threshold = 1500;  // Próg detekcji (dostosuj wg potrzeb)
const int refractoryPeriod = 200; // Okres refrakcji (ms)
unsigned long lastBeatTime = 0;
int bpm = 0;
bool beatDetected = false;
bool config = true;

// Bufor do lekkiego wygładzania
const int smoothWindow = 3;
int rawValues[smoothWindow];
int smoothIndex = 0;

// Zmienne do wykresu
const int graphWidth = 240;
const int graphHeight = 100;
const int graphX = 0;
const int graphY = 20;
int graphBuffer[graphWidth];
int graphIndex = 0;


void conf() {
  while (config) {
    tft.fillRect(0, 0, 240, 135, TFT_BLACK);
    int err = 0;
    if ((digitalRead(37) == 1)){
      err -= 1;
    }
    if ((digitalRead(38) == 1)){
      err -= 2;
    }

    SerialBT.println(err);

    tft.setTextSize(3);
    tft.drawString("ELECTRODES", 30, 0);
    if (err == 0) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("OK", 150, 50);
      tft.drawString("OK", 150, 80);
    }
    else if (err == -2) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("CHECK", 150, 50);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("OK", 150, 80);
    }
    else if (err == -3){
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("CHECK", 150, 50);
      tft.drawString("CHECK", 150, 80);
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("RED: ", 5, 50);
    tft.drawString("YELLOW: ", 0, 80);
    delay(1000);
    if (err == 0) {
      delay(2000);
      tft.fillRect(0, 0, 240, 135, TFT_BLACK);
      config = false;
      break;
    }
    delay(300);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(38, INPUT); //ziel LO-
  pinMode(37, INPUT); //br lo+
  
  // Inicjalizacja ekranu
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  
  // Inicjalizacja Bluetooth
  SerialBT.setPin("1234", 4);
  SerialBT.begin("ESP32-EKG");

  // Inicjalizacja buforów
  for(int i = 0; i < smoothWindow; i++) rawValues[i] = 0;
  for(int i = 0; i < graphWidth; i++) graphBuffer[i] = graphHeight/2;

  conf();
}

void loop() {
  if (!config){
    tft.setTextSize(2);
    tft.drawString("EKG Monitor", 50, 0);
    tft.setTextSize(1);
    tft.drawString("BPM: --", 180, 0);

    int rawValue = analogRead(39);
    
    // Lekkie wygładzanie (3-punktowa średnia ruchoma)
    rawValues[smoothIndex] = rawValue;
    smoothIndex = (smoothIndex + 1) % smoothWindow;
    
    int smoothedValue = 0;
    for(int i = 0; i < smoothWindow; i++) smoothedValue += rawValues[i];
    smoothedValue /= smoothWindow;
    
    // Aktualizacja wykresu (odwrócony, bo wyższe wartości = wyższy sygnał)
    int mappedValue = map(smoothedValue, 0, 4095, graphHeight, 0);
    graphBuffer[graphIndex] = constrain(mappedValue, 0, graphHeight);
    
    // Wykrywanie uderzenia serca
    if(smoothedValue > threshold && millis() - lastBeatTime > refractoryPeriod && !beatDetected) {
      beatDetected = true;
      unsigned long currentTime = millis();
      
      if(lastBeatTime > 0) {
        unsigned long beatInterval = currentTime - lastBeatTime;
        bpm = 60000 / beatInterval;
        
        // Aktualizacja wyświetlacza
        tft.fillRect(180, 0, 60, 16, TFT_BLACK);
        tft.setTextSize(1);
        tft.drawString("BPM: " + String(bpm), 180, 0);
        
        // Wyślij BPM przez Bluetooth
        SerialBT.print("BPM:");
        SerialBT.println(bpm);
      }
      
      lastBeatTime = currentTime;
    }
    
    if(smoothedValue < threshold - 100) { // Histereza
      beatDetected = false;
    }
    
    // Rysowanie wykresu EKG
    tft.fillRect(graphX, graphY, graphWidth, graphHeight, TFT_BLACK);
    for(int i = 1; i < graphWidth; i++) {
      int x1 = (graphIndex + i - 1) % graphWidth;
      int x2 = (graphIndex + i) % graphWidth;
      tft.drawLine(i-1, graphBuffer[x1], i, graphBuffer[x2], TFT_GREEN);
    }
    
    graphIndex = (graphIndex + 1) % graphWidth;
    
    // Wysyłanie danych przez Bluetooth
    SerialBT.println(smoothedValue);
    Serial.println(smoothedValue);
    
    // Opcjonalnie: wyświetlanie surowych danych do debugowania
    // Serial.println(rawValue);
    
    delay(1); // Zachowaj 100Hz próbkowanie
  }
  
}
# ECG Monitor

## Spis treści
- [Opis projektu](#opis-projektu)
- [Funkcjonalności](#funkcjonalności)
- [Schemat układu](#schemat-układu)
- [Wymagania](#wymagania)
- [Jak sklonować i uruchomić](#jak-sklonować-i-uruchomić)
- [Współautorzy](#współautorzy)

---

## Opis projektu

ECG Monitor służy do amatorskiego pomiaru EKG przy użyciu ESP32 + AD8232. Komunikacja pomiędzy płytką a urządzeniem odbywa się poprzez Bluetooth. Pomiar jest dostępny do odczytu na żywo, można część EKG nagrać i odczytać później.

### UWAGA
Zarówno program jak i płytki NIE SĄ urządzeniami medycznymi. Aby uzyskać wiarygodny pomiar a przedewszystkim poradę medyczną, należy udać się do kardiologa.

---

## Funkcjonalności

- Sprawdzanie poprawności podłączenia elektrod
- Odczyt EKG na żywo
- Zapis EKG do pliku i odczyt EKG z pliku

---

## Schemat układu

TBA

---

## Wymagania

- ESP32 (TTGO T-Display)
- Moduł AD8232
- Biblioteki Raylib i Raygui

---

## Jak sklonować i uruchomić

```bash
git clone https://github.com/TheVTV/ECG_Monitor.git
```
- Pobierz biblioteki Raylib i Raygui
- Pobierz Arduino IDE
- Wgraj projekt na płytkę
- Połącz płytkę z komputerem
- Sprawdź na którym porcie COM płytka ma połączenie i ustaw go w kodzie
- Skompiluj ECG_Monitor

---

## Współautorzy

- Bartosz Wójcik

---
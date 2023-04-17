#include <M5Core2.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include "heart1.h"
#include "pulse1.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define MAIN_HEIGHT 200
#define BUTTOM_HEIGHT 40
#define MAIN_WIDTH 150

#define REPORTING_PERIOD_MS 1000

//Struktura do zapisu danych
struct Data {
  uint8_t max; //maksymalna wartosc
  uint8_t min; //minimalna wartosc
};

//Data oraz godzina
RTC_TimeTypeDef RTCtime;
RTC_DateTypeDef RTCDate;

//GLOWNE DANE PROGRAMOWE
struct Data PULSE[24]; //Lista typu Data do zapisu parametrow pulsu z dnia
struct Data SPO2[24]; //Lista typu Data do zapisu parametrow SpO2 z dnia
uint8_t Heart_rate = 0; //Puls chwilowy
uint8_t Spo2 = 0; //SpO2 chwilowe
uint8_t frequency = 1; //Czestotliwosc pomiarow, co ile sekund
uint8_t alert_lv = 180; //Poziom alertu pulsu
uint32_t tsLastReport = 0; //Wartosc czasowa ostatniego pomiaru
uint32_t tsLastSave = 0; // Wartość czasowa ostatniego zapisu
char* screenType[2] = {"Pulse", "Sa02"};

//STANY
bool touchPressed = false;
bool isHomeScreen = true;
bool isSettingsScreen = false;
bool isChangeParamScreen = false;
bool isGraphScreen = false;


//ZMIENNE POMOCNICZE
char path[32]; //buffor na sciezke do pliku
char buff[1012]; //buffor na dane z pliku
uint8_t *changing_param = NULL; //zmienna na wskaznik zmienianego parametru
uint8_t changing_param_value; //Zmienna na wartosc zmienianiego parametru
uint8_t chosenType = 0; //Wybrany typ PULSE, SPO2
TouchPoint_t touchPos;

//CZUJNIKI
PulseOximeter pox;

///PRZYCISKI WIRTUALNE

//Przyciski ekranu startowego
HotZone Btn_Pulse(10, 0, 10 + MAIN_WIDTH, MAIN_HEIGHT); //Przejscie do wykresu plusu
HotZone Btn_Sa02(160, 0, 160 + MAIN_WIDTH, MAIN_HEIGHT); //Przejscie do wykresu SPO2

//Przyciski ekranu ustawień
HotZone Btn_Option1(10, 10, 300, 50); //Przejscie do ustawień poziomu alertu
HotZone Btn_Option2(10, 55, 300, 95); //Przejscie do ustawień czestotliwości

//Przyciski ekranu zmiany wartości parametru
HotZone Btn_Save(50, 200, 270, 240); //Zapis wartości
HotZone Btn_Up(0, 0, 50, 240); //Zwiększenie wartości
HotZone Btn_Dn(270, 0, 320, 240); //Zmniejszenie wartości

//Przyciski emitujące dedykowane przyciski
HotZone Btn_A(0, 200, 100, 240); //Przycisk A
HotZone Btn_B(110, 200, 210, 240); //Przycisk B
HotZone Btn_C(220, 200, 320, 240); //Przycisk C

//Funkcja do restartowania stanow
void resetState()
{
  isHomeScreen = false;
  isSettingsScreen = false;
  isChangeParamScreen = false;
  isGraphScreen = false;
}

//Funkcja potrzebna do działania czujnika
void onBeatDetected()
{
  printHRandSPO2(true);
}

void printHRandSPO2(bool beat)
{
    if( (int)pox.getHeartRate() < 220 && (int)pox.getHeartRate() > 0) 
      Heart_rate = (int)pox.getHeartRate();

    if(pox.getSpO2() < 100 && pox.getSpO2() > 0) 
      Spo2 = pox.getSpO2();
}

//**************************************//
//*** OBSLUGA PLIKU I DANYCH Z PLIKU ***//
//**************************************//

//Funkcja do odczytu danych z pliku
void readDataFromFile(int type)
{ 
  sprintf(buff, "\0");
  M5.Rtc.GetDate(&RTCDate);
  //Przygotowanie sciezki do pliku na podstawie daty oraz parametru
  sprintf(path, "/%02d_%02d_%02d_%s.txt", RTCDate.Date, RTCDate.Month, RTCDate.Year, screenType[type]);

  //Otwarcie pliku do odczytu
  File myFile = SD.open(path);

  if (myFile) {
    Serial.println("OPEN FILE");
    int i = 0;
    while(myFile.available())
    {
      //Pobieranie znaku z pliku
      char inChar = myFile.read();
      //Wpisanie znaku do buffora
      buff[i] = inChar;
      i++;
    }
    buff[i] = '\0';
    //Zamkniecie pliku
    myFile.close();
    Serial.println("CLOSE FILE");

  } else 
  {
    Serial.println("Failed to open file for reading");
  }
}

//Funkcja do zapisu zmiennych z buffora do tablicy danych
void saveDataFromFile(struct Data toSave[]) {
  char param[4] = "   ";
  uint8_t data[3];
  uint8_t data_l = 0;
  uint8_t i = 0;
  for(int z = 0; buff[z] != '\0'; z++)
  { 
    //Gdy znak jest spacja, nowa linia lub zakonczeniem pliku
    //wpisz dane do tablicy
    if(buff[z] == ' ' || buff[z] == '\n' || buff[z] == '\0')
    {
      data[data_l] = atoi(param);
      data_l++;
      if(data_l > 2)
      {
        data_l = 0;
        toSave[data[0]].max = data[1];
        toSave[data[0]].min = data[2];
      }
      param[1] = ' ';
      i = 0;
    } 
    //Wpisywanie znaków do buffora
    else
    {
      param[i] = buff[z];
      i++;
    }
  }
}

void readAndSave(struct Data toSave[], int type)
{
  readDataFromFile(type);
  saveDataFromFile(toSave);
}

//Funkcja do nadpisywania danych z tablicy do pliku
void saveDataToFile(int type) {

  //Wczytanie danych z pliku do buffora
  readDataFromFile(type);

  //Wczytanie ostatniej lini z buffora
  char *lastLine = strrchr(buff, '\n');
  uint8_t N = RTCtime.Hours;
  
  //Modyfikowanie ostatniej lini
  sprintf(lastLine, "%d %d %d", N, PULSE[N].max, PULSE[N].min);

  //Serial.println(lastLine);

  //Otworzenie pliku z obcją pisanie
  File myFile = SD.open("/example.txt", FILE_WRITE);
  if (!myFile) {
    Serial.println("Failed to open file for writing");
    return;
  }

  //Wpisanie zmodyfikowanego buffora do pliku
  myFile.print(buff);

  //Zamkniecie pliku
  myFile.close();
}


//Funcja do przypisania dancy do tablicy
void saveParameters()
{
  M5.Rtc.GetTime(&RTCtime); //odczytanie i przypisanie godziny do zmiennej
  uint8_t N = RTCtime.Hours;

  if(PULSE[N].max != NULL && PULSE[N].min != NULL && Heart_rate > 30)
  {
    if(Heart_rate > PULSE[N].max) PULSE[N].max = Heart_rate;
    if(Heart_rate < PULSE[N].min) PULSE[N].min = Heart_rate;
  } 
  else
  {
    PULSE[N].max = Heart_rate;
    PULSE[N].min = Heart_rate;
  }

  if(SPO2[N].max != NULL && SPO2[N].min != NULL && Spo2 > 30)
  {
    if(Spo2 > SPO2[N].max) SPO2[N].max = Spo2;
    if(Spo2 < SPO2[N].min) SPO2[N].min = Spo2;
  } 
  else
  {
    SPO2[N].max = Spo2;
    SPO2[N].min = Spo2;
  }
}

//************************//
//*** FUNKCJE RYSUJĄCE ***//
//************************//

//Funkcja do rysowania ekranu startowego
void drawStartingScreen()
{
  resetState();
  isHomeScreen = true;
  M5.Lcd.clear();
  M5.Lcd.setTextSize(3);


  M5.Lcd.setCursor(30, 25);
  M5.Lcd.print("Pulse");

  M5.Lcd.setCursor(200, 25);
  M5.Lcd.print("Sa02");

  M5.Lcd.drawBitmap(25, 60, 96, 96, heart1); //rysowanie ikony pulsu
  M5.Lcd.drawBitmap(185, 60, 96, 96, pulse1); //rysowanie ikony dla SpO2
}

//Funkcja wypisujaca wartosci chwilowe na ekranie startowym
void drawParameters()
{
    M5.Lcd.fillRect(0, 160, 320, 40, BLACK);
    M5.Lcd.setTextSize(4);

    //Wypisywanie parametru pulsu na ekranie
    M5.Lcd.setCursor(50, MAIN_HEIGHT - 40);
    M5.Lcd.print(Heart_rate);

    //Wypisywanie parametru SpO2 na ekranie
    M5.Lcd.setCursor(210, MAIN_HEIGHT - 40);
    M5.Lcd.print(Spo2);

    //Sprawdzanie czy wlaczyc alert
    vibrationAlert(Heart_rate);
}

//Funkcja do rysowania osi na wykresie
void drawAxes() 
{
  M5.Lcd.drawLine(10, 10, 10, 210, WHITE);
  M5.Lcd.drawLine(10, 210, 310, 210, WHITE);
}

//Funkcja rysujaca wykres slupkowy, parametry przyjmowane
//struktura danych oraz typ 0 -> PULSE, 1 -> SPO2
void  drawGraph(struct Data toDraw[], int type) {
  uint8_t max = 0;

  //Ustawianie skali do wykresow
  if(type == 0) max = 220; //220 zakladane maksymalne tetno
  if(type == 1) max = 100; //SPO2 podawane jest w %

  //toDraw[23].min = 50;

  for(int i=0; i<24; i++)
  {
    if(toDraw[i].max != NULL && toDraw[i].min != NULL &&  toDraw[i].max + toDraw[i].min != 0)
    {
      uint8_t y0 = 10 + ((max - toDraw[i].max)*200)/max; //y0 slupka
      uint8_t height = ((toDraw[i].max - toDraw[i].min)*200)/max; //wysokosc slupka
      M5.Lcd.fillRoundRect(15 + 12*i, y0, 8, height, 0, RED);
    }

    //Rysowanie wartosci na osi Y
    if( i%4 == 3) 
    {
      M5.Lcd.setTextSize(2);
      M5.Lcd.setCursor(15 + 12*i, 220);
      M5.Lcd.print(i+1);
    }
  }
}

//Funkcja do rysowania ekrany z wykresem
void drawGraphScreen(int type)
{
  chosenType = type;
  resetState();
  isGraphScreen = true;
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(120, 0);
  M5.Lcd.print(screenType[type]);
  drawAxes();
  if(type == 0) drawGraph(PULSE, type);
  if(type == 1) drawGraph(SPO2, type);

}

//Funkcja do rysowania ekranu ustawien
void drawSettingsScreen()
{
  resetState();
  isSettingsScreen = true;
  M5.Lcd.clear();
  M5.Lcd.setTextDatum(5);
  M5.Lcd.setTextSize(2);

  //Wizalizacja przycisku do ustawiania poziomu alertu
  M5.Lcd.drawRect(10, 10, 300, 40, WHITE);
  M5.Lcd.setCursor(15, 20);
  M5.Lcd.print("Set pulse alert");

  //Wizalizacja przycisku do ustawiania czestotliwosci pomiaru
  M5.Lcd.drawRect(10, 55, 300, 40, WHITE);
  M5.Lcd.setCursor(15, 65);
  M5.Lcd.print("Set frequency");
}

//Funkcja do rysowania ekranu zmiany parametru
void drawChangeParamScreen()
{
  resetState();
  isChangeParamScreen = true;
  M5.Lcd.clear();
  M5.Lcd.setCursor(80, 0);
  M5.Lcd.print("Set param");

  //Wizalizacja przycisku akceptacji zapisu
  M5.Lcd.fillRect(50, 200, 220, 40, GREEN);
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setCursor(140, 210);
  M5.Lcd.print("SAVE");

  //Wizalizacja przycisku zwiekszania wartosci
  M5.Lcd.drawRect(0, 0, 50, 240, WHITE);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(20, 120);
  M5.Lcd.print("+");

  //Wizalizacja przycisku zmniejszania wartosci
  M5.Lcd.drawRect(270, 0, 50, 240, WHITE);
  M5.Lcd.setCursor(290, 120);
  M5.Lcd.print("-");
  //Rysowanie wartosc zmienianiego parametru
  newParamValue();
}

//Funkcja do rysowania parametru z ekranu zmiany
void newParamValue()
{
  M5.Lcd.setTextSize(6);
  M5.Lcd.setCursor(140, 120);
  M5.Lcd.fillRect(100, 100, 150, 80, BLACK);
  M5.Lcd.print(changing_param_value);
  M5.Lcd.setTextSize(2);
}

//Funkcja do rysowania tebeli z wartosciami w zaleznisci
//od wybranego parametru
void drawTable(struct Data data[])
{ 
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(RED);
  
  //Wypisywanie MAX i MIN do tabeli
  for(int i = 0; i < 3; i++)
  {
    M5.Lcd.setCursor(20 + 110*i,0); 
    M5.Lcd.print("MAX");

    M5.Lcd.setCursor(60 + 110*i,0); 
    M5.Lcd.print("MIN");
  }

  //Rysowanie lin pionowych
  M5.Lcd.drawLine(15, 0, 15, 240, WHITE);
  M5.Lcd.drawLine(97, 0, 97, 240, WHITE);
  M5.Lcd.drawLine(125, 0, 125, 240, WHITE);
  M5.Lcd.drawLine(207, 0, 207, 240, WHITE);
  M5.Lcd.drawLine(235, 0, 235, 240, WHITE);
  M5.Lcd.drawLine(58, 0, 58, 240, WHITE);
  M5.Lcd.drawLine(168, 0, 168, 240, WHITE);
  M5.Lcd.drawLine(278, 0, 278, 240, WHITE);

  for(int i = 0; i < 24; i++)
  { 
    M5.Lcd.setTextColor(WHITE);

    //Rysowanie pierwszej kolumny
    if(i<8)
    {
      M5.Lcd.drawLine(0, 26*i + 21, 320, 26*i + 21, WHITE);
      M5.Lcd.setCursor(20, 26*i + 26); 
      M5.Lcd.print(data[i].max);
      M5.Lcd.setCursor(60, 26*i + 26); 
      M5.Lcd.print(data[i].min);
      M5.Lcd.setCursor(0, 26*i + 26);

    } else if(i<16) //Rysowanie drugiej kolumny
    {
      M5.Lcd.setCursor(130, 26*(i-8) + 26); 
      M5.Lcd.print(data[i].max);
      M5.Lcd.setCursor(170, 26*(i-8) + 26); 
      M5.Lcd.print(data[i].min);
      M5.Lcd.setCursor(100, 26*(i-8) + 26);  
    }
    else //Rysowanie trzeciej kolumny
    {
      M5.Lcd.setCursor(240, 26*(i-16) + 26); 
      M5.Lcd.print(data[i].max);
      M5.Lcd.setCursor(280, 26*(i-16) + 26); 
      M5.Lcd.print(data[i].min);
      M5.Lcd.setCursor(210, 26*(i-16) + 26);  
    }
    M5.Lcd.setTextColor(RED);

    //Wypisanie godziny do tabeli
    M5.Lcd.print(i);
  }
  M5.Lcd.setTextColor(WHITE);
}

//Funkcja do rysowania ekranu z tabela
void drawTableScreen(int type)
{
  if(type == 0) drawTable(PULSE);
  if(type == 1) drawTable(SPO2);
}

void vibrationAlert(int hr)
{
  if(hr > alert_lv) M5.Axp.SetLDOEnable(3, true);
  else M5.Axp.SetLDOEnable(3, false);
}

void setup()
{
  M5.begin();
  Wire.begin();
  Serial.begin(9600);
  SD.begin();

  //Inicjalizacja czujnika
  pox.begin();
  //Ustawienie odpowiedniego sposobu dzialania czujnika
  pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
  //Na wykrycie pulsu wywolanie funkcji
  pox.setOnBeatDetectedCallback(onBeatDetected);

  //Rysowanie ekranu startowego
  drawStartingScreen();
  
  //Pobranie i zapisanie dacnych
  readAndSave(PULSE, 0);
  readAndSave(SPO2, 1);
}

//*** Glowna petla programu ***//
void loop()
{
  M5.update();
  pox.update();
  touchPos = M5.Touch.getPressPoint();

  //Sprawdzanie czy ekran został dotkniety
  if ((touchPos.x != -1) && (touchPressed == false)) {
    touchPressed = true;
    
    //Jesli wyswietlany jest ekran poczatkowy, sprawdza czy
    //dotkniety zostal, ktorys z przyciskow
    if(isHomeScreen == true)
    {
      //Gdy dotkniety zostal przycisk, ktorys z przyciskow
      //rysowany jest ekran z wykresem
      if (Btn_Pulse.inHotZone(touchPos)) drawGraphScreen(0);
      if (Btn_Sa02.inHotZone(touchPos))  drawGraphScreen(1);
    }

    //Jesli wyswietlany jest ekran ustawien, sprawdza czy
    //dotkniety zostal, ktorys z przyciskow    
    if(isSettingsScreen == true)
    {
      //Sprawdzanie czy dotkniety zostal przycisk do ustawien
      //alertu
      if (Btn_Option1.inHotZone(touchPos))  
        {
          //Przypiusywanie obecnej wartosci poziomu alertu
          //i jej adresu, do zmiennych pomocniczych
          changing_param = &alert_lv;
          changing_param_value = *changing_param;

          //Rysowanie ekranu zmiany parametru
          drawChangeParamScreen();
        }
      
      //Sprawdzanie czy dotkniety zostal przycisk do ustawien
      //czestotliwosci
      if (Btn_Option2.inHotZone(touchPos))
        {
          //Przypiusywanie obecnej wartosci czestotliwosci
          //i jej adresu, do zmiennych pomocniczych
          changing_param = &frequency;
          changing_param_value = *changing_param;

          //Rysowanie ekranu zmiany parametru
          drawChangeParamScreen();        
        }
    }

    //Jesli wyswietlany jest ekran zmiany parametru, sprawdza czy
    //dotkniety zostal, ktorys z przyciskow 
    if(isChangeParamScreen == true)
    {
      //Sprawdzanie czy dotkniety zostal przycisk zwiekszajacy wartosc
      //parametru
      if (Btn_Up.inHotZone(touchPos) && changing_param_value >= 0)  
      {
        changing_param_value += 1;
        newParamValue();
      }

      //Sprawdzanie czy dotkniety zostal przycisk zmniejszajacy wartosc
      //parametru
      if (Btn_Dn.inHotZone(touchPos) && changing_param_value >= 0)  
      {
        changing_param_value -= 1;
        if(changing_param_value < 0) changing_param_value = 0;
        newParamValue();
      }

      //Sprawdzanie czy dotkniety zostal przycisk do zapisu wartosci
      if (Btn_Save.inHotZone(touchPos))  
      {
        *changing_param = changing_param_value;
      }
    }

    //Obsluga wirtualnych przyciskow emitujacych, przyciski dedykowane

    //Przycisk A odpowiedzialny za rysowanie ekranu startowego
    if (Btn_A.inHotZone(touchPos) && !isChangeParamScreen) drawStartingScreen();

    //Przycisk B odpowiedzialny za rysowanie ekranu ustawien
    if (Btn_B.inHotZone(touchPos)) drawSettingsScreen();

    ////Przycisk C odpowiedzialny za rysowanie ekranu z tablica wartosci
    if (Btn_C.inHotZone(touchPos) && isGraphScreen && !isChangeParamScreen) drawTableScreen(chosenType);
  }
  else if (touchPos.x == -1) {
    touchPressed = false;
  }

  //Przycisk A odpowiedzialny za rysowanie ekranu startowego
  if (M5.BtnA.wasReleased() || M5.BtnA.pressedFor(1000, 200)) drawStartingScreen();

  //Przycisk B odpowiedzialny za rysowanie ekranu ustawien
  if (M5.BtnB.wasReleased() || M5.BtnB.pressedFor(1000, 200)) drawSettingsScreen();

  ////Przycisk C odpowiedzialny za rysowanie ekranu z tablica wartosci
  if ((M5.BtnC.wasReleased() || M5.BtnC.pressedFor(1000, 200)) && isGraphScreen)  drawTableScreen(chosenType);

  if(isHomeScreen)
  {
    //Wypisywanie wartosci na ekranie startowym co okreslony czas
    if (millis() - tsLastReport > REPORTING_PERIOD_MS * frequency)
    {
      drawParameters();
      saveParameters();
      tsLastReport = millis();
    }
    /*
    //Zapisywanie danych do pliku co okreslony czas
    if (millis() - tsLastSave > REPORTING_PERIOD_MS * 10)
    { 
      pox.shutdown();
      Serial.println("SAVE DATA");
      saveDataToFile(0);
      tsLastSave = millis();
      pox.resume();
    }
    */ 
  }
}

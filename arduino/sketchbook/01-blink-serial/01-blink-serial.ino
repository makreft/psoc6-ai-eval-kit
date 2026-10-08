// Erster Test des Arduino-Cores auf dem CY8CKIT-062S2-AI:
// LED blinkt, Konsole an J1 (KitProg3-UART, 115200 Baud) meldet sich jede Sekunde.
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println("CY8CKIT-062S2-AI: Arduino-Core laeuft");
}

unsigned long n = 0;
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  Serial.print("tick ");
  Serial.println(n++);
}

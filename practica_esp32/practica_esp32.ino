#include <ESP32Servo.h>
#include "EEPROM.h"
#include <esp_sleep.h>
#include <WiFi.h>

const char* ssid = "Bruno";
const char* password = "BRUNOTFG";

// Se inicializa un servidor en el puerto estándar 80
WiFiServer server(80);
String header;

Servo servoMotor;   // Crea un objeto servo llamado servoMotor
const int pinServo = 2;
const int echoPin = 26;
const int trigPin = 25;
int i = 0;
int direccion_EEPROM_angulo = 0;
int direccion_EEPROM_mapa = 1;
int angulo_guardado;

const long VENTANA_SERVIDOR = 5000;   // ms con la página disponible

void setup(){ 
  Serial.begin(115200); 
  // Inicializa 20 bytes de memoria EEPROM (1 para el angulo (0 o 180) y 19 para el mapa)
  EEPROM.begin(20);
  // Asociamos el servo a la patilla 2 del Arduino    
  servoMotor.attach(pinServo);
  // inicializa los pines digitales del sensor ultrasónico
  pinMode(echoPin, INPUT);             
  pinMode(trigPin, OUTPUT);

  // Lee el último sentido del barrido guardado para alternarlo en el siguiente loop
  angulo_guardado = EEPROM.read(direccion_EEPROM_angulo); 
  Serial.printf("Angulo guardado: %d\n", angulo_guardado);
}
 
void loop(){   
  if(angulo_guardado == 180) {  // Barrido decreciente de 180° a 0°
    servoMotor.write(180);
    delay(500);
    for (i=180;i>=0;i-=10) {
      medirYGuardar(i);
    }
    EEPROM.write(direccion_EEPROM_angulo,0);
  } else {  // Barrido creciente de 0° a 180°
    servoMotor.write(0);
    delay(500);
    for (i=0;i<=180;i+=10) {
      medirYGuardar(i);
    }
    EEPROM.write(direccion_EEPROM_angulo,180);
  }

  EEPROM.commit();

  imprimirMapa();
  servidorWeb();

  // Al finalizar medición e impresión por servidor,
  // se ejecutan 5s de sueño profundo
  // Deespierta transcurridos 5s
  Serial.println("Entrando en Sueño Profundo");
  esp_deep_sleep(5 * 1000000);

  // Uso sueño profundo porque simula un escenario real de bajo consumo (por ejemplo colocado en un pasillo alimentado con una batería).
  // El dispositivo no necesita mantener el servidor web abierto ni la CPU activa de forma continua,
  // ya que los trabajadores solo consultarían el mapa de obstáculos de manera puntual.
  // Dormir el chip por completo evita el consumo innecesario de energía y asegura que el sistema sea autónomo y eficiente.
  // Al despertar del sueño profundo, el sistema se reinicia por completo. Esto deja los pines y el servomotor listos en su posición inicial
  // asegurando que cada nuevo escaneo del mapa de obstáculos empiece en un entorno totalmente limpio y sin interferencias del ciclo anterior.
}


void medirYGuardar(int ang) {
  servoMotor.write(ang);
  delay(500);     // Me aseguro de que llegue el servo antes de medir
  float d = readDistance(); // Calculo la distancia

  uint8_t valor;  // Para pasar de un float (4 bytes) a a un entero sin signo de 1 byte
  valor = (uint8_t)(d + 0.5);          // redondeo al entero más cercano

  // Vamos guardando en el mapa los valores de las distancias por cada 10 angulos
  EEPROM.write(direccion_EEPROM_mapa + ang / 10, valor);
  Serial.printf("Angulo %3d -> %d\n", ang, valor);
}

void imprimirMapa() {
  Serial.println("--- MAPA EN EEPROM ---");
  for (int a = 0; a <= 180; a += 10) {
    uint8_t v = EEPROM.read(direccion_EEPROM_mapa + a / 10);
    Serial.printf("%3d grados: %d cm\n", a, v);
  }
  Serial.println("----------------------");
}

void servidorWeb() {
  //ESP32 intenta conectarse a la red ssid y con password mediante WiFi.begin(ssid, password); 
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  unsigned long inicio = millis();
  //Se establece un bucle de espera con tiempo limite 10 segundos para verificar el estado de la conexion
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nSin WiFi");
    return;
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // Se obtiene la IP local para acceder a la web
  server.begin();

  unsigned long t0 = millis();
  // El servidor permanece abierto el timepo VENTANA_SERVIDOR
  while (millis() - t0 < VENTANA_SERVIDOR) {
    atenderCliente(); // Procesa las peticiones HTTP entrantes
  }

  // Cuando pasan los 5s el servidor se detiene y el Wi-Fi se desconecta
  server.stop();
  WiFi.disconnect(true);
}

void atenderCliente() {
  // Cuando un navegador accede a la IP, se genera una página HTML
  // con una tabla en la que se ve el mapa creado y almacenado en la EEPROM anteriormente 
  WiFiClient client = server.available();
  if (!client) return;

  unsigned long previousTime = millis();
  unsigned long currentTime = previousTime;
  String currentLine = "";

  while (client.connected() && currentTime - previousTime <= 2000) {
    currentTime = millis();
    if (client.available()) {
      char c = client.read();
      header += c;
      if (c == '\n') {
        if (currentLine.length() == 0) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println("Connection: close");
          client.println();

          client.println("<!DOCTYPE html><html>");
          client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
          client.println("<link rel=\"icon\" href=\"data:,\">");
          client.println("<style>html { font-family: Times New Roman; display: inline-block; margin: 0px auto; text-align: center;}");
          client.println("table { margin: 0 auto; border-collapse: collapse; }");
          client.println("td, th { border: 1px solid #333; padding: 4px 16px; }");
          client.println(".obs { background-color: #f4a6a6; }</style></head>");
          client.println("<body><h1>Mapa de obstaculos</h1>");
          client.println("<table><tr><th>Angulo</th><th>Distancia</th></tr>");
          for (int a = 0; a <= 180; a += 10) {
            uint8_t v = EEPROM.read(direccion_EEPROM_mapa + a / 10);
            if (v == 255) client.println("<tr><td>" + String(a) + "</td><td>sin dato</td></tr>");
            else if (v < 20) client.println("<tr class=\"obs\"><td>" + String(a) + "</td><td>" + String(v) + " cm (obstaculo)</td></tr>");
            else client.println("<tr><td>" + String(a) + "</td><td>" + String(v) + " cm</td></tr>");
          }
          // Se refresca automáticamente la página
          client.println("</table>");
          client.println("<script>let ok=true;setInterval(()=>{const c=new AbortController();setTimeout(()=>c.abort(),2000);fetch('/',{cache:'no-store',signal:c.signal}).then(()=>{if(!ok)location.reload();ok=true;}).catch(()=>{ok=false;});},3000);</script>");
          client.println("</body></html>");
          client.println();
          break;
        } else {
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }
  header = "";
  client.stop();
}

float readDistance() {
  digitalWrite(trigPin, LOW);   // Low para garantizar un pulso limpio
  delayMicroseconds(2);         
  digitalWrite(trigPin, HIGH);  // Pulso de 10us
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);  

  // Medir el pulso y calcular la distancia
  float duracion = pulseIn(echoPin, HIGH);  // Mide cuanto tiempo dura el rebote (Devuelve microsegundos)
  float distancia = duracion / 29.2 / 2;  // El sonido tarda unos 29,2 µs en recorrer 1 cm. /2 porque solo queremos saber los cm de ida no de ida y vuelta
  return distancia;
}

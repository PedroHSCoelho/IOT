// MODELO de configuracao. Copie este arquivo para "config.h" (na mesma pasta) e preencha.
// O "config.h" NUNCA vai para o Git (esta no .gitignore): ele tem a senha do Wi-Fi.

#define WIFI_SSID   "nome-do-hotspot"      // use a rede 2,4 GHz (o ESP32 nao enxerga 5 GHz)
#define WIFI_SENHA  "senha-do-wifi"

// Endereco (IP) do computador onde o broker MQTT esta rodando (npm run broker).
// No Windows: abra o Prompt e digite "ipconfig"; use o "Endereco IPv4" da rede Wi-Fi.
#define MQTT_HOST   "192.168.0.10"
#define MQTT_PORTA  1883

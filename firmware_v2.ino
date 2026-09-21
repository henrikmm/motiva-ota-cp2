/* ============================================================================
   S2-CP02 - Projeto Motiva | Atualizacao Remota de Firmware (OTA)
   FIRMWARE 2.0 - No de sensoriamento de vegetacao (ESP32 / Wokwi)

   Integrantes:
     Helena Barbosa Costa                  RM 562450
     Henrique Mandrick                     RM 562715
     Mateus Scandiuzzi Valente Tomomitsu   RM 561565
     Ryan Amorim de Castro Santana         RM 564393
     Thomas Joh Kobayashi                  RM 562758

   Evolucao em relacao ao Firmware 1.0 (secao 7 do enunciado):
     - mantem as 5 leituras / 2 s e o calculo da media
     - ordena uma COPIA das leituras em ordem crescente (algoritmo proprio)
     - exibe os valores na ordem original e na ordem crescente
     - calcula e exibe a MEDIANA da sessao
     - aplica HISTERESE sobre a mediana para alternar NORMAL / ALERTA
     - LED VERDE = NORMAL, LED VERMELHO = ALERTA
     - verifica o manifesto no boot e reporta que ja esta na versao mais recente

   Comandos de teste pelo Serial Monitor (digite e tecle Enter):
     T1 -> injeta sessao com mediana >= 16 cm  (espera-se ALERTA / vermelho)
     T2 -> injeta sessao com mediana  = 15 cm  (espera-se MANTER o estado)
     T3 -> injeta sessao com mediana <= 14 cm  (espera-se NORMAL / verde)
   ============================================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

/* ------------------------------------------------------------- IDENTIDADE */
static const char *FW_VERSION = "2.0";

/* ------------------------------------------------------------------ REDE */
static const char    *WIFI_SSID       = "Wokwi-GUEST";
static const char    *WIFI_PASS       = "";
static const uint8_t  WIFI_CANAL      = 6;
static const uint32_t WIFI_TIMEOUT_MS = 20000UL;

static const char *MANIFEST_URL =
    "https://raw.githubusercontent.com/henrikmm/motiva-ota-cp2/main/version.json";

/* -------------------------------------------------------------- HARDWARE */
static const uint8_t PIN_LED_R = 25;
static const uint8_t PIN_LED_G = 26;
static const uint8_t PIN_LED_B = 27;

/* ----------------------------------------------------------- TEMPORIZACAO */
static const uint8_t  LEITURAS_POR_SESSAO  = 5;
static const uint32_t INTERVALO_LEITURA_MS = 2000UL;
static const uint32_t INTERVALO_SESSAO_MS  = 48000UL;

/* ---------------------------------------------------------------- MEDICAO */
static const int ALTURA_MIN_CM = 10;
static const int ALTURA_MAX_CM = 20;

/* --------------------------------------------------------------- HISTERESE
   Secao 9 do enunciado:
     mediana >= 16 cm ............ entra em ALERTA
     14 cm < mediana < 16 cm ..... mantem o estado anterior
     mediana <= 14 cm ............ entra/retorna para NORMAL
   Como as leituras sao numeros inteiros, a mediana de 5 valores tambem e
   sempre inteira: a faixa de manutencao contem unicamente a mediana 15 cm. */
static const int LIMITE_ALERTA = 16;
static const int LIMITE_NORMAL = 14;

enum EstadoSistema { ESTADO_NORMAL, ESTADO_ALERTA };

/* ------------------------------------------------------------------ ESTADO */
static int           leituras[LEITURAS_POR_SESSAO];
static uint8_t       indiceLeitura         = 0;
static uint32_t      tInicioSessao         = 0;
static uint32_t      tInicioSessaoAnterior = 0;
static uint32_t      tProximaLeitura       = 0;
static uint32_t      tProximaSessao        = 0;
static uint16_t      sessoesConcluidas     = 0;
static bool          sessaoEmAndamento     = false;
static EstadoSistema estadoAtual           = ESTADO_NORMAL;

/* ===========================================================================
   COMPARACAO DE TEMPO SEGURA CONTRA OVERFLOW DE millis()
   =========================================================================== */
static bool tempoAlcancado(uint32_t alvo) {
  return (int32_t)(millis() - alvo) >= 0;
}

/* ===========================================================================
   LED
   =========================================================================== */
static void configurarLed() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
}

static void definirCor(bool vermelho, bool verde, bool azul) {
  digitalWrite(PIN_LED_R, vermelho ? HIGH : LOW);
  digitalWrite(PIN_LED_G, verde    ? HIGH : LOW);
  digitalWrite(PIN_LED_B, azul     ? HIGH : LOW);
}

/* Secao 10: no FW 2.0 o LED passa a indicar o ESTADO do sistema */
static void atualizarLedPorEstado() {
  if (estadoAtual == ESTADO_ALERTA) definirCor(true,  false, false);  /* vermelho */
  else                              definirCor(false, true,  false);  /* verde    */
}

static const char *nomeEstado(EstadoSistema e) {
  return (e == ESTADO_ALERTA) ? "ALERTA" : "NORMAL";
}

static const char *nomeCorDoEstado(EstadoSistema e) {
  return (e == ESTADO_ALERTA) ? "VERMELHO" : "VERDE";
}

/* ===========================================================================
   AQUISICAO E ESTATISTICA
   =========================================================================== */
static int gerarLeitura() {
  return random(ALTURA_MIN_CM, ALTURA_MAX_CM + 1);
}

static float calcularMedia(const int valores[], uint8_t n) {
  long soma = 0;
  for (uint8_t i = 0; i < n; i++) soma += valores[i];
  return (float)soma / (float)n;
}

/* Ordenacao por insercao, implementada no proprio programa (secao 8).
   Trabalha sobre uma COPIA para preservar a ordem original das leituras. */
static void ordenarCrescente(int destino[], const int origem[], uint8_t n) {
  for (uint8_t i = 0; i < n; i++) destino[i] = origem[i];

  for (uint8_t i = 1; i < n; i++) {
    int chave = destino[i];
    int j = (int)i - 1;
    while (j >= 0 && destino[j] > chave) {
      destino[j + 1] = destino[j];
      j--;
    }
    destino[j + 1] = chave;
  }
}

/* Para 5 valores, a mediana e o elemento central do vetor ordenado */
static int calcularMediana(const int ordenado[], uint8_t n) {
  return ordenado[n / 2];
}

static void imprimirVetor(const char *rotulo, const int valores[], uint8_t n) {
  Serial.print(rotulo);
  for (uint8_t i = 0; i < n; i++) {
    Serial.printf("%d", valores[i]);
    if (i < n - 1) Serial.print("  ");
  }
  Serial.println(" cm");
}

/* ===========================================================================
   HISTERESE
   =========================================================================== */
static EstadoSistema aplicarHisterese(int mediana, const char **motivo) {
  if (mediana >= LIMITE_ALERTA) {
    *motivo = "mediana >= 16 cm -> entra em ALERTA";
    estadoAtual = ESTADO_ALERTA;
  } else if (mediana <= LIMITE_NORMAL) {
    *motivo = "mediana <= 14 cm -> entra/retorna para NORMAL";
    estadoAtual = ESTADO_NORMAL;
  } else {
    *motivo = "14 cm < mediana < 16 cm -> faixa de histerese, estado mantido";
    /* estadoAtual permanece inalterado */
  }
  return estadoAtual;
}

/* ===========================================================================
   PROCESSAMENTO DE UMA SESSAO COMPLETA
   =========================================================================== */
static void processarResultadosDaSessao(const int valores[], uint8_t n) {
  int ordenado[LEITURAS_POR_SESSAO];
  ordenarCrescente(ordenado, valores, n);

  float media   = calcularMedia(valores, n);
  int   mediana = calcularMediana(ordenado, n);

  EstadoSistema estadoAnterior = estadoAtual;
  const char   *motivo         = "";
  EstadoSistema estadoNovo     = aplicarHisterese(mediana, &motivo);
  atualizarLedPorEstado();

  imprimirVetor("Valores na ordem original : ", valores,  n);
  imprimirVetor("Valores em ordem crescente: ", ordenado, n);
  Serial.printf("Media da sessao: %.1f cm\n", media);
  Serial.printf("Mediana da sessao: %d cm\n", mediana);
  Serial.printf("Histerese: %s\n", motivo);
  Serial.printf("Estado anterior: %s | Estado atual: %s\n",
                nomeEstado(estadoAnterior), nomeEstado(estadoNovo));
  Serial.printf("LED: %s\n", nomeCorDoEstado(estadoNovo));
}

/* ===========================================================================
   CICLO DE MEDICAO (nao bloqueante, baseado em millis)
   =========================================================================== */
static void imprimirCabecalhoSessao() {
  float agoraS = millis() / 1000.0f;
  float delta  = (tInicioSessaoAnterior == 0)
                     ? 0.0f
                     : (tInicioSessao - tInicioSessaoAnterior) / 1000.0f;

  Serial.println();
  Serial.println("========================================");
  Serial.printf ("MONITORAMENTO DE VEGETACAO - FW %s\n", FW_VERSION);
  Serial.println("========================================");
  Serial.printf ("Sessao %u | t = %.2f s | intervalo desde a sessao anterior: %.2f s\n",
                 (unsigned)(sessoesConcluidas + 1), agoraS, delta);
}

static void iniciarSessao() {
  tInicioSessaoAnterior = tInicioSessao;
  tInicioSessao         = millis();
  tProximaSessao        = tInicioSessao + INTERVALO_SESSAO_MS;  /* ancorado no INICIO */
  tProximaLeitura       = tInicioSessao;
  indiceLeitura         = 0;
  sessaoEmAndamento     = true;

  imprimirCabecalhoSessao();
}

static void finalizarSessao() {
  processarResultadosDaSessao(leituras, LEITURAS_POR_SESSAO);
  Serial.printf("Proxima sessao em %lu segundos.\n", INTERVALO_SESSAO_MS / 1000UL);

  sessaoEmAndamento = false;
  sessoesConcluidas++;
}

static void processarSessao() {
  if (!tempoAlcancado(tProximaLeitura)) return;

  leituras[indiceLeitura] = gerarLeitura();
  Serial.printf("Leitura %u: %d cm\n", (unsigned)(indiceLeitura + 1), leituras[indiceLeitura]);

  indiceLeitura++;
  tProximaLeitura += INTERVALO_LEITURA_MS;

  if (indiceLeitura >= LEITURAS_POR_SESSAO) finalizarSessao();
}

/* ===========================================================================
   COMANDOS DE TESTE PELO SERIAL (testes 6, 7 e 8 de forma deterministica)
   =========================================================================== */
static void executarSessaoDeTeste(const int valores[], const char *rotulo) {
  Serial.println();
  Serial.println("----------------------------------------");
  Serial.printf ("SESSAO DE TESTE INJETADA - %s\n", rotulo);
  Serial.println("----------------------------------------");
  for (uint8_t i = 0; i < LEITURAS_POR_SESSAO; i++) {
    Serial.printf("Leitura %u: %d cm\n", (unsigned)(i + 1), valores[i]);
  }
  processarResultadosDaSessao(valores, LEITURAS_POR_SESSAO);
}

static void processarComandosSeriais() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "T1") {
    const int v[LEITURAS_POR_SESSAO] = {18, 16, 17, 20, 16};   /* mediana 17 */
    executarSessaoDeTeste(v, "T1: mediana >= 16 cm");
  } else if (cmd == "T2") {
    const int v[LEITURAS_POR_SESSAO] = {13, 15, 20, 15, 12};   /* mediana 15 */
    executarSessaoDeTeste(v, "T2: 14 cm < mediana < 16 cm");
  } else if (cmd == "T3") {
    const int v[LEITURAS_POR_SESSAO] = {10, 14, 13, 12, 14};   /* mediana 13 */
    executarSessaoDeTeste(v, "T3: mediana <= 14 cm");
  } else {
    Serial.printf("Comando desconhecido: \"%s\". Use T1, T2 ou T3.\n", cmd.c_str());
  }
}

/* ===========================================================================
   VERIFICACAO DE VERSAO NO BOOT
   Comprova a situacao "a versao instalada ja e a mais recente" (secao 13).
   =========================================================================== */
static String extrairValorJson(const String &json, const String &chave) {
  String alvo = "\"" + chave + "\"";
  int p = json.indexOf(alvo);
  if (p < 0) return "";
  p = json.indexOf(':', p + alvo.length());
  if (p < 0) return "";
  int ini = json.indexOf('"', p);
  if (ini < 0) return "";
  int fim = json.indexOf('"', ini + 1);
  if (fim < 0) return "";
  return json.substring(ini + 1, fim);
}

static int versaoParaNumero(const String &versao) {
  int ponto = versao.indexOf('.');
  int maior = (ponto < 0) ? versao.toInt() : versao.substring(0, ponto).toInt();
  int menor = (ponto < 0) ? 0              : versao.substring(ponto + 1).toInt();
  return maior * 100 + menor;
}

static bool conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("[OTA] Conectando a rede \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);

  uint32_t limite = millis() + WIFI_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && !tempoAlcancado(limite)) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] ERRO: nao foi possivel conectar ao Wi-Fi.");
    Serial.println("[OTA] O no segue medindo normalmente com o FW 2.0.");
    return false;
  }

  Serial.printf("[OTA] Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static void verificarVersaoInstalada() {
  Serial.println();
  Serial.println("----------------------------------------");
  Serial.println("VERIFICACAO DE VERSAO APOS O BOOT");
  Serial.printf ("Versao instalada: %s\n", FW_VERSION);
  Serial.println("----------------------------------------");

  if (!conectarWiFi()) return;

  WiFiClientSecure cliente;
  cliente.setInsecure();
  cliente.setTimeout(20000);

  HTTPClient http;
  if (!http.begin(cliente, MANIFEST_URL)) {
    Serial.println("[OTA] ERRO: URL do manifesto invalida.");
    return;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("[OTA] ERRO: manifesto inacessivel (HTTP %d - %s).\n",
                  codigo, http.errorToString(codigo).c_str());
    http.end();
    return;
  }

  String corpo = http.getString();
  http.end();

  String versaoRemota = extrairValorJson(corpo, "version");
  Serial.printf("[OTA] Versao instalada: %s | versao disponivel: %s\n",
                FW_VERSION, versaoRemota.c_str());

  if (versaoParaNumero(versaoRemota) > versaoParaNumero(FW_VERSION)) {
    Serial.println("[OTA] Existe uma versao mais nova publicada no repositorio.");
  } else {
    Serial.println("[OTA] Firmware ja esta na versao mais recente. Nenhuma acao necessaria.");
  }
}

/* ===========================================================================
   SETUP / LOOP
   =========================================================================== */
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(300);

  configurarLed();
  estadoAtual = ESTADO_NORMAL;
  atualizarLedPorEstado();

  randomSeed(esp_random());

  Serial.println();
  Serial.println("########################################");
  Serial.printf ("  PROJETO MOTIVA - FIRMWARE %s\n", FW_VERSION);
  Serial.println("  Atualizacao OTA concluida com sucesso");
  Serial.println("  Novos recursos: ordenacao, mediana e histerese");
  Serial.println("  Comandos de teste: T1 / T2 / T3");
  Serial.println("########################################");

  verificarVersaoInstalada();

  tProximaSessao = millis();
}

void loop() {
  processarComandosSeriais();

  if (!sessaoEmAndamento && tempoAlcancado(tProximaSessao)) {
    iniciarSessao();
  }

  if (sessaoEmAndamento) {
    processarSessao();
  }
}

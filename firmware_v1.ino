/* ============================================================================
   S2-CP02 - Projeto Motiva | Atualizacao Remota de Firmware (OTA)
   FIRMWARE 1.0 - No de sensoriamento de vegetacao (ESP32 / Wokwi)

   Integrantes:
     Helena Barbosa Costa                  RM 562450
     Henrique Mandrick                     RM 562715
     Mateus Scandiuzzi Valente Tomomitsu   RM 561565
     Ryan Amorim de Castro Santana         RM 564393
     Thomas Joh Kobayashi                  RM 562758

   Funcao desta versao:
     - 5 leituras pseudoaleatorias (10 a 20 cm) por sessao, uma a cada 2 s
     - media aritmetica da sessao
     - nova sessao a cada 48 s contados do INICIO da sessao anterior
     - LED AZUL indicando "firmware 1.0 em execucao"
     - apos 3 sessoes, consulta o manifesto remoto e, havendo versao mais
       nova, baixa o .bin e executa a atualizacao OTA
   ============================================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

/* ------------------------------------------------------------- IDENTIDADE */
static const char *FW_VERSION = "1.0";

/* ------------------------------------------------------------------ REDE */
static const char    *WIFI_SSID       = "Wokwi-GUEST";
static const char    *WIFI_PASS       = "";
static const uint8_t  WIFI_CANAL      = 6;          /* acelera o connect no Wokwi */
static const uint32_t WIFI_TIMEOUT_MS = 20000UL;

static const char *MANIFEST_URL =
    "https://raw.githubusercontent.com/henrikmm/motiva-ota-cp2/main/version.json";

/* -------------------------------------------------------------- HARDWARE */
/* LED RGB de catodo comum: nivel ALTO acende o canal */
static const uint8_t PIN_LED_R = 25;
static const uint8_t PIN_LED_G = 26;
static const uint8_t PIN_LED_B = 27;

/* ----------------------------------------------------------- TEMPORIZACAO */
static const uint8_t  LEITURAS_POR_SESSAO  = 5;
static const uint32_t INTERVALO_LEITURA_MS = 2000UL;   /* 2 s   (2 min reais)  */
static const uint32_t INTERVALO_SESSAO_MS  = 48000UL;  /* 48 s  (48 h reais)   */
static const uint8_t  CICLOS_ANTES_OTA     = 3;

/* ---------------------------------------------------------------- MEDICAO */
static const int ALTURA_MIN_CM = 10;
static const int ALTURA_MAX_CM = 20;

/* ------------------------------------------------------------------ ESTADO */
static int      leituras[LEITURAS_POR_SESSAO];
static uint8_t  indiceLeitura        = 0;
static uint32_t tInicioSessao        = 0;
static uint32_t tInicioSessaoAnterior= 0;
static uint32_t tProximaLeitura      = 0;
static uint32_t tProximaSessao       = 0;
static uint16_t sessoesConcluidas    = 0;
static bool     sessaoEmAndamento    = false;
static bool     otaJaTentada         = false;

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

/* Azul = firmware 1.0 em execucao (secao 10 do enunciado) */
static void indicarFirmwareAtivo() { definirCor(false, false, true); }

/* Magenta = processo de atualizacao OTA em andamento */
static void indicarAtualizacao()   { definirCor(true, false, true); }

/* ===========================================================================
   AQUISICAO E ESTATISTICA
   =========================================================================== */
static int gerarLeitura() {
  return random(ALTURA_MIN_CM, ALTURA_MAX_CM + 1);  /* 10..20 cm inclusive */
}

static float calcularMedia(const int valores[], uint8_t n) {
  long soma = 0;
  for (uint8_t i = 0; i < n; i++) soma += valores[i];
  return (float)soma / (float)n;
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
  /* a proxima sessao e ancorada no INICIO desta, nao no fim da 5a leitura */
  tProximaSessao        = tInicioSessao + INTERVALO_SESSAO_MS;
  tProximaLeitura       = tInicioSessao;
  indiceLeitura         = 0;
  sessaoEmAndamento     = true;

  imprimirCabecalhoSessao();
}

static void finalizarSessao() {
  float media = calcularMedia(leituras, LEITURAS_POR_SESSAO);
  Serial.printf("Media da sessao: %.1f cm\n", media);
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
   CONECTIVIDADE
   =========================================================================== */
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
    /* Situacao prevista na secao 13: nao ha conexao Wi-Fi */
    Serial.println("[OTA] ERRO: nao foi possivel conectar ao Wi-Fi.");
    Serial.println("[OTA] Atualizacao cancelada. O no continua medindo com o FW 1.0.");
    return false;
  }

  Serial.printf("[OTA] Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

/* ===========================================================================
   MANIFESTO DE VERSAO
   Leitura manual do JSON: o manifesto tem formato fixo e conhecido, o que
   dispensa uma biblioteca de parsing externa e mantem o binario menor.
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

static bool consultarManifesto(String &versaoRemota, String &urlFirmware) {
  WiFiClientSecure cliente;
  cliente.setInsecure();          /* Wokwi: sem validacao de cadeia de certificados */
  cliente.setTimeout(20000);

  HTTPClient http;
  Serial.printf("[OTA] Consultando manifesto: %s\n", MANIFEST_URL);

  if (!http.begin(cliente, MANIFEST_URL)) {
    Serial.println("[OTA] ERRO: URL do manifesto invalida.");
    return false;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    /* Situacao prevista na secao 13: o manifesto nao pode ser acessado */
    Serial.printf("[OTA] ERRO: manifesto inacessivel (HTTP %d - %s).\n",
                  codigo, http.errorToString(codigo).c_str());
    http.end();
    return false;
  }

  String corpo = http.getString();
  http.end();

  Serial.println("[OTA] Manifesto recebido:");
  Serial.println(corpo);

  versaoRemota = extrairValorJson(corpo, "version");
  urlFirmware  = extrairValorJson(corpo, "url");

  if (versaoRemota.length() == 0 || urlFirmware.length() == 0) {
    Serial.println("[OTA] ERRO: manifesto sem os campos \"version\" e \"url\".");
    return false;
  }
  return true;
}

/* Converte "2.0" em 200 para permitir comparacao numerica (e nao textual) */
static int versaoParaNumero(const String &versao) {
  int ponto = versao.indexOf('.');
  int maior = (ponto < 0) ? versao.toInt()  : versao.substring(0, ponto).toInt();
  int menor = (ponto < 0) ? 0               : versao.substring(ponto + 1).toInt();
  return maior * 100 + menor;
}

static bool versaoEhMaisNova(const String &remota, const String &instalada) {
  return versaoParaNumero(remota) > versaoParaNumero(instalada);
}

/* ===========================================================================
   ATUALIZACAO OTA
   =========================================================================== */
static void aoProgredir(int recebido, int total) {
  static int ultimoPercentual = -1;
  if (total <= 0) return;
  int p = (recebido * 100) / total;
  if (p != ultimoPercentual && p % 10 == 0) {
    Serial.printf("[OTA] Download: %d%% (%d / %d bytes)\n", p, recebido, total);
    ultimoPercentual = p;
  }
}

static void executarAtualizacaoOTA(const String &urlFirmware) {
  Serial.printf("[OTA] Baixando firmware: %s\n", urlFirmware.c_str());
  indicarAtualizacao();

  WiFiClientSecure cliente;
  cliente.setInsecure();
  cliente.setTimeout(30000);

  httpUpdate.rebootOnUpdate(false);   /* o reboot e feito por nos, apos o log */
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.onProgress(aoProgredir);

  t_httpUpdate_return resultado = httpUpdate.update(cliente, urlFirmware);

  switch (resultado) {
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] Gravacao concluida com sucesso.");
      Serial.println("[OTA] Reiniciando o ESP32 para executar o Firmware 2.0...");
      Serial.flush();
      delay(500);
      ESP.restart();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      /* Situacao prevista na secao 13: a versao instalada ja e a mais recente */
      Serial.println("[OTA] Servidor informou que nao ha atualizacao a aplicar.");
      indicarFirmwareAtivo();
      break;

    case HTTP_UPDATE_FAILED:
    default:
      /* Situacoes previstas na secao 13: download falhou / gravacao com erro */
      Serial.printf("[OTA] ERRO %d durante a atualizacao: %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      Serial.println("[OTA] O no permanece no FW 1.0 e continua medindo normalmente.");
      indicarFirmwareAtivo();
      break;
  }
}

/* Orquestra todo o fluxo obrigatorio da secao 12 */
static void verificarAtualizacao() {
  otaJaTentada = true;

  Serial.println();
  Serial.println("----------------------------------------");
  Serial.printf ("VERIFICACAO DE ATUALIZACAO (apos %u sessoes)\n", (unsigned)sessoesConcluidas);
  Serial.printf ("Versao instalada: %s\n", FW_VERSION);
  Serial.println("----------------------------------------");

  if (!conectarWiFi()) { indicarFirmwareAtivo(); return; }

  String versaoRemota, urlFirmware;
  if (!consultarManifesto(versaoRemota, urlFirmware)) { indicarFirmwareAtivo(); return; }

  Serial.printf("[OTA] Versao instalada: %s | versao disponivel: %s\n",
                FW_VERSION, versaoRemota.c_str());

  if (!versaoEhMaisNova(versaoRemota, FW_VERSION)) {
    /* Situacao prevista na secao 13: a versao instalada ja e a mais recente */
    Serial.println("[OTA] Firmware ja esta na versao mais recente. Nenhuma acao necessaria.");
    indicarFirmwareAtivo();
    return;
  }

  Serial.println("[OTA] Atualizacao disponivel. Iniciando download...");
  executarAtualizacaoOTA(urlFirmware);
}

/* ===========================================================================
   SETUP / LOOP
   =========================================================================== */
void setup() {
  Serial.begin(115200);
  delay(300);

  configurarLed();
  indicarFirmwareAtivo();

  randomSeed(esp_random());

  Serial.println();
  Serial.println("########################################");
  Serial.printf ("  PROJETO MOTIVA - FIRMWARE %s\n", FW_VERSION);
  Serial.println("  No de monitoramento de vegetacao");
  Serial.printf ("  Verificacao OTA apos %u sessoes\n", (unsigned)CICLOS_ANTES_OTA);
  Serial.println("########################################");

  tProximaSessao = millis();   /* primeira sessao comeca imediatamente */
}

void loop() {
  if (!sessaoEmAndamento && tempoAlcancado(tProximaSessao)) {
    iniciarSessao();
  }

  if (sessaoEmAndamento) {
    processarSessao();
  }

  if (!sessaoEmAndamento && !otaJaTentada && sessoesConcluidas >= CICLOS_ANTES_OTA) {
    verificarAtualizacao();
    /* a verificacao e bloqueante; se ela ultrapassou a janela de 48 s,
       reancora o agendamento para nao disparar sessoes em rajada */
    if (tempoAlcancado(tProximaSessao)) tProximaSessao = millis();
  }
}

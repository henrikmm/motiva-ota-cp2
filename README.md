# Projeto Motiva — Atualização Remota de Firmware (OTA)

**S2-CP02 — Professor Marcelo Fernando Morgantini**
Nó IoT de monitoramento de vegetação em ESP32 simulado no Wokwi, com atualização
remota de firmware a partir de um repositório público.

---

## Integrantes

| Nome | RM |
|------|-----|
| Helena Barbosa Costa | 562450 |
| Henrique Mandrick | 562715 |
| Mateus Scandiuzzi Valente Tomomitsu | 561565 |
| Ryan Amorim de Castro Santana | 564393 |
| Thomas Joh Kobayashi | 562758 |

---

## Links da entrega

| Item | Link |
|------|------|
| Projeto Wokwi (público) | https://wokwi.com/projects/475816561802161153 |
| Repositório remoto (OTA) | https://github.com/henrikmm/motiva-ota-cp2 |
| Manifesto de versão | https://raw.githubusercontent.com/henrikmm/motiva-ota-cp2/main/version.json |
| Binário da versão 2.0 | https://raw.githubusercontent.com/henrikmm/motiva-ota-cp2/main/firmware_v2.bin |

---

## Arquitetura da solução

```
   ESP32 / Wokwi                 Internet                Repositório remoto
  ┌──────────────┐              (Wokwi-GUEST)           ┌──────────────────┐
  │ Firmware 1.0 │ ──── HTTPS GET version.json ───────► │ version.json     │
  │              │ ◄─── { "version": "2.0", ... } ───── │                  │
  │  compara     │                                      │                  │
  │  1.0 < 2.0   │ ──── HTTPS GET firmware_v2.bin ────► │ firmware_v2.bin  │
  │              │ ◄─── 1.025.120 bytes ─────────────── │                  │
  │ grava OTA    │                                      └──────────────────┘
  │ ESP.restart()│
  └──────┬───────┘
         │
  ┌──────▼───────┐
  │ Firmware 2.0 │  ordenação + mediana + histerese
  └──────────────┘
```

**Fluxo:** consultar versão → comparar → baixar `.bin` → gravar → reiniciar.

### Particionamento e por que o OTA funciona

O ESP32 é compilado no esquema **Default 4MB with spiffs**, que reserva duas
partições de aplicação de **1.310.720 bytes** (`app0` e `app1`). O firmware em
execução ocupa uma delas; a biblioteca `HTTPUpdate` grava o binário recebido na
outra e marca, na partição `otadata`, qual delas deve ser carregada no próximo
boot. Por isso o `ESP.restart()` já sobe na versão 2.0.

O `firmware_v2.bin` ocupa **1.025.120 bytes (78 %)** da partição — cabe com
folga. O `build.sh` falha propositalmente se esse limite for ultrapassado.

### Hardware simulado

| Componente | Ligação |
|------------|---------|
| ESP32 DevKit C v4 | — |
| LED RGB (cátodo comum) — canal R | GPIO 25 → resistor 220 Ω |
| LED RGB (cátodo comum) — canal G | GPIO 26 → resistor 220 Ω |
| LED RGB (cátodo comum) — canal B | GPIO 27 → resistor 220 Ω |
| LED RGB — COM | GND |

### Indicação por LED

| Situação | Cor |
|----------|-----|
| Firmware 1.0 em execução | Azul |
| Download/gravação OTA em andamento | Magenta |
| Firmware 2.0 — estado NORMAL | Verde |
| Firmware 2.0 — estado ALERTA | Vermelho |

---

## Estrutura do repositório

```
motiva-ota-cp2/
├── version.json        manifesto de versão consultado pelo ESP32
├── firmware_v2.bin     binário publicado para a atualização OTA
├── firmware_v1.ino     código-fonte da versão 1.0
├── firmware_v2.ino     código-fonte da versão 2.0
├── diagram.json        circuito do projeto Wokwi
├── build.sh            script de compilação reprodutível
├── gerar_relatorio.py  gera o PDF de entrega a partir deste README/código
└── docs/               evidências reais dos testes obrigatórios (ver abaixo)
```

---

## Evidências e metodologia de validação

A fila de compilação gratuita do editor web do Wokwi apresentou instabilidade recorrente
("Build Servers Busy" / "Failed to fetch") durante os testes do grupo. Para validar a solução sem
depender dela, os firmwares foram compilados localmente com o `arduino-cli` (o mesmo binário
publicado no repositório) e a simulação completa foi executada pela **[Wokwi CLI](https://docs.wokwi.com/wokwi-ci/introduction)**
(`wokwi-cli`), que usa o mesmo motor de simulação em nuvem do editor web — mesma rede `Wokwi-GUEST`,
mesmo acesso real à internet, mesma gravação OTA.

Os logs abaixo são a saída **real e integral** do Serial Monitor, sem edição de conteúdo:

- [`docs/01_serial_fw1_fluxo_ota_completo.txt`](docs/01_serial_fw1_fluxo_ota_completo.txt) — boot do
  FW 1.0, três sessões com intervalo de exatamente **48,00 s**, consulta ao manifesto real no
  GitHub, download de 1.025.120 bytes, gravação OTA e reboot (`SW_CPU_RESET`) já executando o FW 2.0.
- [`docs/02_serial_fw2_histerese_T1_T2_T3_T2.txt`](docs/02_serial_fw2_histerese_T1_T2_T3_T2.txt) —
  FW 2.0 confirmando que já está na versão mais recente, e a sequência `T1 → T2 → T3 → T2`
  demonstrando a histerese nos dois sentidos (mantém ALERTA depois do T1, mantém NORMAL depois do T3).
- [`docs/03_led_azul_fw1.png`](docs/03_led_azul_fw1.png) — captura do LED RGB azul durante a
  execução do Firmware 1.0.

O projeto público no link da tabela acima roda exatamente o mesmo código e pode ser reexecutado a
qualquer momento clicando em **Play**.

---

## Conceitos aplicados

### Média
Soma das 5 leituras dividida por 5. É sensível a valores extremos: uma única
leitura muito alta desloca a média inteira.

### Mediana
Valor central do vetor **ordenado**. Com 5 leituras, é o terceiro elemento
(`ordenado[2]`). A ordenação é feita no próprio programa, por **inserção**,
sobre uma cópia do vetor — a ordem original é preservada para exibição.

A mediana é usada na decisão de estado justamente por ser **robusta a outliers**:
uma leitura espúria de 20 cm no meio de um canteiro de 12 cm move a média, mas
não move a mediana. Em um sensor de campo, isso evita alarmes falsos.

### Histerese
Impede que o sistema oscile entre NORMAL e ALERTA quando a medida fica em cima
do limite.

| Condição | Comportamento |
|----------|---------------|
| mediana ≥ 16 cm | entra em ALERTA |
| 14 cm < mediana < 16 cm | **mantém o estado anterior** |
| mediana ≤ 14 cm | entra/retorna para NORMAL |

Sem histerese, uma vegetação oscilando entre 15 e 16 cm faria o LED piscar entre
verde e vermelho a cada sessão. Com a faixa morta, o estado só muda quando a
medida ultrapassa uma das bordas de forma decisiva.

> **Observação sobre o enunciado:** o "exemplo de comportamento" da seção 9 cita
> medianas de 14,5 cm. Como as leituras são inteiras (10 a 20 cm), a mediana de
> cinco valores é **sempre inteira** — a faixa de manutenção contém unicamente a
> mediana **15 cm**. A implementação segue a tabela normativa, que é a regra.

### Por que OTA é útil em campo
Os nós da Motiva ficam instalados em áreas de difícil acesso, por vezes a
quilômetros de estrada. Sem OTA, corrigir um cálculo errado ou mudar um limiar
exigiria deslocar uma equipe até cada equipamento, com custo de viagem, risco
operacional e janela de indisponibilidade. Com OTA, a correção é publicada uma
vez no repositório e todos os nós se atualizam sozinhos na próxima janela de
comunicação. É a diferença entre uma manutenção de campo e um `git push`.

---

## Temporização das sessões

O enunciado exige que a nova sessão comece **48 s após o início da anterior**, e
não 48 s depois da quinta leitura:

```
00 s → leitura 1      (5 leituras × 2 s = 8 s de aquisição)
02 s → leitura 2
04 s → leitura 3
06 s → leitura 4
08 s → leitura 5
48 s → início da sessão seguinte      ← e não 56 s
```

A implementação **não usa `delay()` longo**. Cada sessão, ao começar, já agenda a
próxima em `tInicioSessao + 48000`, e o `loop()` compara o relógio com
`millis()`. Toda comparação usa `(int32_t)(millis() - alvo) >= 0`, forma que
continua correta quando `millis()` estoura em 32 bits (a cada ~49 dias).

Cada cabeçalho de sessão imprime o instante e o intervalo medido desde a sessão
anterior, o que comprova o requisito diretamente no Serial Monitor.

---

## Bibliotecas utilizadas

| Biblioteca | Origem | Função no projeto |
|------------|--------|-------------------|
| `WiFi.h` | core ESP32 | conecta à rede virtual `Wokwi-GUEST` |
| `WiFiClientSecure.h` | core ESP32 | socket TLS para falar com o GitHub via HTTPS |
| `HTTPClient.h` | core ESP32 | requisição GET do `version.json` |
| `HTTPUpdate.h` | core ESP32 | baixa o `.bin` e grava na partição OTA inativa |

**Nenhuma biblioteca de terceiros é usada.** O `version.json` tem formato fixo e
conhecido, então é lido por uma função própria (`extrairValorJson`) em vez de um
parser JSON externo — isso elimina uma dependência, reduz o binário e mantém o
projeto compilável no Wokwi sem configuração adicional.

A comparação de versões é **numérica**, não textual: `"2.0"` vira `200` e `"1.0"`
vira `100`. Comparar strings quebraria em um futuro `"10.0"`, que é textualmente
menor que `"2.0"`.

---

## Tratamento de situações previstas (seção 13)

| Situação | Mensagem no Serial Monitor |
|----------|----------------------------|
| Sem conexão Wi-Fi | `[OTA] ERRO: nao foi possivel conectar ao Wi-Fi.` |
| Manifesto inacessível | `[OTA] ERRO: manifesto inacessivel (HTTP <código>).` |
| Manifesto malformado | `[OTA] ERRO: manifesto sem os campos "version" e "url".` |
| Já está na versão mais recente | `[OTA] Firmware ja esta na versao mais recente.` |
| Download ou gravação falhou | `[OTA] ERRO <n> durante a atualizacao: <descrição>` |

Em todos os casos de falha o nó **continua medindo normalmente** na versão atual.
Uma atualização que não deu certo nunca derruba o serviço de medição.

---

## Como executar

### No Wokwi (demonstração)

1. Abrir o projeto Wokwi do grupo (link na tabela acima).
2. Clicar em **Play**. O LED acende **azul** e o Serial Monitor mostra o FW 1.0.
3. Aguardar **3 sessões completas** (≈ 104 s). O nó conecta ao Wi-Fi, consulta o
   manifesto, detecta a versão 2.0 e inicia o download (LED **magenta**).
4. Após a gravação o ESP32 reinicia sozinho no **Firmware 2.0**, e o LED passa a
   indicar o estado (**verde** NORMAL / **vermelho** ALERTA).
5. Para testar a histerese de forma determinística, digitar no Serial Monitor:
   `T1` (mediana ≥ 16), `T2` (mediana = 15), `T3` (mediana ≤ 14).

### Compilando localmente

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
./build.sh
```

O `build.sh` compila as duas versões, gera o `firmware_v2.bin`, imprime tamanho e
SHA-256 e aborta se o binário não couber na partição de aplicação.

---

## Comandos de teste do Firmware 2.0

Os estados de histerese dependem da mediana, que é pseudoaleatória. Para tornar
os testes 6, 7 e 8 reproduzíveis, o firmware aceita comandos pelo Serial Monitor:

| Comando | Leituras injetadas | Mediana | Resultado esperado |
|---------|--------------------|---------|--------------------|
| `T1` | 18 16 17 20 16 | 17 cm | → ALERTA (vermelho) |
| `T2` | 13 15 20 15 12 | 15 cm | mantém o estado anterior |
| `T3` | 10 14 13 12 14 | 13 cm | → NORMAL (verde) |

A sequência `T1 → T2 → T3 → T2` demonstra a histerese **nos dois sentidos**: o
`T2` mantém ALERTA quando vem depois do `T1`, e mantém NORMAL quando vem depois
do `T3`. É o mesmo valor de mediana produzindo estados diferentes — que é
exatamente o que histerese significa.

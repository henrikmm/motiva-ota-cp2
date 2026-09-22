#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gera o relatorio PDF da entrega (secao 15 do enunciado)."""
import html, json, os, subprocess, sys, hashlib, datetime, glob

RAIZ = os.path.dirname(os.path.abspath(__file__))
WOKWI = "https://wokwi.com/projects/475816561802161153"
REPO  = "https://github.com/henrikmm/motiva-ota-cp2"
RAW   = "https://raw.githubusercontent.com/henrikmm/motiva-ota-cp2/main"

INTEGRANTES = [
    ("Helena Barbosa Costa", "562450"),
    ("Henrique Mandrick", "562715"),
    ("Mateus Scandiuzzi Valente Tomomitsu", "561565"),
    ("Ryan Amorim de Castro Santana", "564393"),
    ("Thomas Joh Kobayashi", "562758"),
]

def ler(p):
    with open(os.path.join(RAIZ, p), encoding="utf-8") as f:
        return f.read()

def bloco_codigo(texto, numerar=True):
    linhas = texto.rstrip("\n").split("\n")
    if numerar:
        largura = len(str(len(linhas)))
        corpo = "\n".join(
            f'<span class="ln">{str(i+1).rjust(largura)}</span> {html.escape(l)}'
            for i, l in enumerate(linhas))
    else:
        corpo = html.escape(texto)
    return f'<pre class="code">{corpo}</pre>'

def tabela(cabs, linhas):
    th = "".join(f"<th>{html.escape(c)}</th>" for c in cabs)
    tr = "".join(
        "<tr>" + "".join(f"<td>{c if c.startswith('<') else html.escape(c)}</td>"
                          for c in ln) + "</tr>"
        for ln in linhas)
    return f"<table><thead><tr>{th}</tr></thead><tbody>{tr}</tbody></table>"

# ----------------------------------------------------------------- evidencias
def secao_evidencias():
    partes = []
    logs = sorted(glob.glob(os.path.join(RAIZ, "docs", "*.txt")))
    imgs = sorted(glob.glob(os.path.join(RAIZ, "docs", "*.png")))
    for img in imgs:
        nome = os.path.basename(img)
        rel = f"docs/{nome}"
        legenda = nome.replace("_", " ").rsplit(".", 1)[0]
        partes.append(
            f'<figure><img src="{rel}"/><figcaption>{html.escape(legenda)}</figcaption></figure>')
    for log in logs:
        nome = os.path.basename(log)
        partes.append(f"<h3>{html.escape(nome)}</h3>")
        partes.append(bloco_codigo(ler(f"docs/{nome}"), numerar=False))
    return "\n".join(partes) if partes else "<p><em>Sem evidencias capturadas.</em></p>"

# ------------------------------------------------------------------ binario
binpath = os.path.join(RAIZ, "firmware_v2.bin")
bintam = os.path.getsize(binpath)
binsha = hashlib.sha256(open(binpath, "rb").read()).hexdigest()

manifesto = json.loads(ler("version.json"))

CSS = """
@page { size: A4; margin: 16mm 14mm; }
* { box-sizing: border-box; }
body { font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif;
       font-size: 10pt; line-height: 1.5; color: #1a1a1a; margin: 0; }
h1 { font-size: 20pt; margin: 0 0 4px; letter-spacing: -.3px; }
h2 { font-size: 13pt; margin: 22px 0 8px; padding-bottom: 5px;
     border-bottom: 2px solid #c8102e; page-break-after: avoid; }
h3 { font-size: 11pt; margin: 14px 0 6px; page-break-after: avoid; }
p  { margin: 6px 0; }
.sub { color: #555; font-size: 10.5pt; margin-bottom: 2px; }
.capa { border-left: 4px solid #c8102e; padding-left: 14px; margin-bottom: 18px; }
table { border-collapse: collapse; width: 100%; margin: 8px 0 12px; font-size: 9.5pt; }
th { background: #f0f0f2; text-align: left; font-weight: 600; }
th, td { border: 1px solid #d5d5da; padding: 5px 8px; vertical-align: top; }
td.ok { color: #0a7a35; font-weight: 600; }
pre.code { background: #f7f7f9; border: 1px solid #e0e0e6; border-radius: 4px;
           padding: 8px 10px; font-family: 'SF Mono', Menlo, Consolas, monospace;
           font-size: 6.6pt; line-height: 1.35; white-space: pre-wrap;
           word-break: break-word; overflow-wrap: anywhere; }
pre.code .ln { color: #b0b0b8; user-select: none; }
a { color: #0b57d0; text-decoration: none; word-break: break-all; }
figure { margin: 10px 0; page-break-inside: avoid; }
figure img { width: 100%; border: 1px solid #d5d5da; border-radius: 4px; }
figcaption { font-size: 8.5pt; color: #666; margin-top: 3px; text-align: center; }
.quebra { page-break-before: always; }
.nota { background: #fff8e1; border-left: 3px solid #f0ad00;
        padding: 7px 11px; margin: 10px 0; font-size: 9.5pt; }
.ascii { font-family: 'SF Mono', Menlo, Consolas, monospace; font-size: 8pt;
         line-height: 1.3; background: #f7f7f9; border: 1px solid #e0e0e6;
         border-radius: 4px; padding: 10px; white-space: pre; }
"""

ARQ = """
   ESP32 / Wokwi                 Internet                Repositorio remoto
  +--------------+              (Wokwi-GUEST)           +------------------+
  | Firmware 1.0 | ---- HTTPS GET version.json -------> | version.json     |
  |              | <--- { "version": "2.0", ... } ----- |                  |
  |  compara     |                                      |                  |
  |  1.0 < 2.0   | ---- HTTPS GET firmware_v2.bin ----> | firmware_v2.bin  |
  |              | <--- 1.025.120 bytes --------------- |                  |
  | grava OTA    |                                      +------------------+
  | ESP.restart()|
  +------+-------+
         |
  +------v-------+
  | Firmware 2.0 |  ordenacao + mediana + histerese
  +--------------+
"""

testes = [
    ("1", "Firmware 1.0", "5 leituras, media e LED da versao 1.0"),
    ("2", "Sessao completa", "Nova sessao iniciando em 48 s, e nao 56 s"),
    ("3", "Manifesto indica versao 2.0", "ESP32 identifica atualizacao disponivel, apos 3 ciclos"),
    ("4", "OTA executada", "ESP32 reinicia executando Firmware 2.0"),
    ("5", "Firmware 2.0", "Media, ordenacao e mediana exibidas corretamente"),
    ("6", "Mediana >= 16 cm", "Estado ALERTA e indicacao vermelha"),
    ("7", "Mediana entre 14 e 16 cm", "Estado anterior mantido"),
    ("8", "Mediana <= 14 cm", "Estado NORMAL e indicacao verde"),
]

doc = f"""<!DOCTYPE html>
<html lang="pt-BR"><head><meta charset="utf-8">
<title>S2-CP02 Projeto Motiva - OTA</title><style>{CSS}</style></head><body>

<div class="capa">
<h1>Projeto Motiva &mdash; Atualizacao Remota de Firmware (OTA)</h1>
<p class="sub">S2-CP02 &middot; Professor Marcelo Fernando Morgantini</p>
<p class="sub">No IoT de monitoramento de vegetacao em ESP32 simulado no Wokwi</p>
<p class="sub">{datetime.date.today().strftime('%d/%m/%Y')}</p>
</div>

<h2>1. Integrantes</h2>
{tabela(["Nome","RM"], INTEGRANTES)}

<h2>2. Links da entrega</h2>
{tabela(["Item","Link"], [
  ("Projeto Wokwi (publico)", f'<a href="{WOKWI}">{WOKWI}</a>'),
  ("Repositorio remoto (OTA)", f'<a href="{REPO}">{REPO}</a>'),
  ("Manifesto de versao", f'<a href="{RAW}/version.json">{RAW}/version.json</a>'),
  ("Binario da versao 2.0", f'<a href="{RAW}/firmware_v2.bin">{RAW}/firmware_v2.bin</a>'),
])}
<p>A estrutura permanece ativa e publica por, no minimo, 10 dias para validacao.</p>

<h2>3. Arquitetura da solucao</h2>
<div class="ascii">{html.escape(ARQ.strip())}</div>
<p><strong>Fluxo:</strong> consultar versao &rarr; comparar &rarr; baixar .bin &rarr; atualizar &rarr; reiniciar.</p>

<h3>Particionamento e por que o OTA funciona</h3>
<p>O ESP32 e compilado no esquema <strong>Default 4MB with spiffs</strong>, que reserva duas
particoes de aplicacao de <strong>1.310.720 bytes</strong> (<code>app0</code> e <code>app1</code>).
O firmware em execucao ocupa uma delas; a biblioteca <code>HTTPUpdate</code> grava o binario
recebido na outra e marca, na particao <code>otadata</code>, qual deve ser carregada no proximo
boot. Por isso o <code>ESP.restart()</code> ja sobe na versao 2.0.</p>
<p>O <code>firmware_v2.bin</code> ocupa <strong>{bintam:,} bytes ({bintam*100//1310720}%)</strong>
da particao. O <code>build.sh</code> falha propositalmente se esse limite for ultrapassado.</p>
<div class="nota"><strong>Detalhe observado na demonstracao:</strong> apos o <code>ESP.restart()</code>
do OTA, o contador de <code>millis()</code> do Firmware 2.0 nao reiniciou em zero (ver secao 9): ele
continuou a partir de onde estava antes do reset. Isso e comportamento real do ESP32 &mdash; um
reset por software (<code>SW_CPU_RESET</code>) reinicia a CPU mas nao zera o dominio de tempo da
RTC, diferente de um power-on reset (<code>POWERON_RESET</code>), que zera tudo. O log confirma os
dois tipos de reset nos momentos certos.</div>

<h3>Hardware simulado</h3>
{tabela(["Componente","Ligacao"], [
  ("ESP32 DevKit C v4", "-"),
  ("LED RGB (catodo comum) - canal R", "GPIO 25 -> resistor 220 ohm"),
  ("LED RGB (catodo comum) - canal G", "GPIO 26 -> resistor 220 ohm"),
  ("LED RGB (catodo comum) - canal B", "GPIO 27 -> resistor 220 ohm"),
  ("LED RGB - COM", "GND"),
])}

<h3>Indicacao por LED</h3>
{tabela(["Situacao","Cor"], [
  ("Firmware 1.0 em execucao", "Azul"),
  ("Download/gravacao OTA em andamento", "Magenta"),
  ("Firmware 2.0 - estado NORMAL", "Verde"),
  ("Firmware 2.0 - estado ALERTA", "Vermelho"),
])}

<h2>4. Temporizacao das sessoes</h2>
<p>O enunciado exige que a nova sessao comece <strong>48 s apos o inicio da anterior</strong>,
e nao 48 s depois da quinta leitura:</p>
<div class="ascii">00 s -> leitura 1      (5 leituras x 2 s = 8 s de aquisicao)
02 s -> leitura 2
04 s -> leitura 3
06 s -> leitura 4
08 s -> leitura 5
48 s -> inicio da sessao seguinte      &lt;- e nao 56 s</div>
<p>A implementacao <strong>nao usa <code>delay()</code> longo</strong>. Cada sessao, ao comecar, ja
agenda a proxima em <code>tInicioSessao + 48000</code>, e o <code>loop()</code> compara o relogio
com <code>millis()</code>. Toda comparacao usa <code>(int32_t)(millis() - alvo) &gt;= 0</code>, forma
que continua correta quando <code>millis()</code> estoura em 32 bits (a cada ~49 dias).</p>
<p>Cada cabecalho de sessao imprime o instante e o intervalo medido desde a sessao anterior,
o que comprova o requisito diretamente no Serial Monitor.</p>

<h2>5. Conceitos aplicados</h2>
<h3>Media</h3>
<p>Soma das 5 leituras dividida por 5. E sensivel a valores extremos: uma unica leitura muito
alta desloca a media inteira.</p>
<h3>Mediana</h3>
<p>Valor central do vetor <strong>ordenado</strong>. Com 5 leituras, e o terceiro elemento
(<code>ordenado[2]</code>). A ordenacao e feita no proprio programa, por <strong>insercao</strong>,
sobre uma copia do vetor &mdash; a ordem original e preservada para exibicao.</p>
<p>A mediana e usada na decisao de estado por ser <strong>robusta a outliers</strong>: uma leitura
espuria de 20 cm no meio de um canteiro de 12 cm move a media, mas nao move a mediana. Em um
sensor de campo, isso evita alarmes falsos.</p>
<h3>Histerese</h3>
{tabela(["Condicao","Comportamento"], [
  ("mediana >= 16 cm", "entra em ALERTA"),
  ("14 cm < mediana < 16 cm", "mantem o estado anterior"),
  ("mediana <= 14 cm", "entra/retorna para NORMAL"),
])}
<p>Sem histerese, uma vegetacao oscilando entre 15 e 16 cm faria o LED piscar entre verde e
vermelho a cada sessao. Com a faixa morta, o estado so muda quando a medida ultrapassa uma das
bordas de forma decisiva.</p>
<div class="nota"><strong>Observacao sobre o enunciado:</strong> o "exemplo de comportamento" da
secao 9 cita medianas de 14,5 cm. Como as leituras sao inteiras (10 a 20 cm), a mediana de cinco
valores e <strong>sempre inteira</strong> &mdash; a faixa de manutencao contem unicamente a mediana
<strong>15 cm</strong>. A implementacao segue a tabela normativa, que e a regra.</div>
<h3>Por que OTA e util em campo</h3>
<p>Os nos da Motiva ficam instalados em areas de dificil acesso, por vezes a quilometros de
estrada. Sem OTA, corrigir um calculo errado ou mudar um limiar exigiria deslocar uma equipe ate
cada equipamento, com custo de viagem, risco operacional e janela de indisponibilidade. Com OTA,
a correcao e publicada uma vez no repositorio e todos os nos se atualizam sozinhos na proxima
janela de comunicacao.</p>

<h2>6. Bibliotecas utilizadas</h2>
{tabela(["Biblioteca","Origem","Funcao no projeto"], [
  ("WiFi.h", "core ESP32", "conecta a rede virtual Wokwi-GUEST"),
  ("WiFiClientSecure.h", "core ESP32", "socket TLS para falar com o GitHub via HTTPS"),
  ("HTTPClient.h", "core ESP32", "requisicao GET do version.json"),
  ("HTTPUpdate.h", "core ESP32", "baixa o .bin e grava na particao OTA inativa"),
])}
<p><strong>Nenhuma biblioteca de terceiros e usada.</strong> O <code>version.json</code> tem formato
fixo e conhecido, entao e lido por uma funcao propria (<code>extrairValorJson</code>) em vez de um
parser JSON externo &mdash; isso elimina uma dependencia, reduz o binario e mantem o projeto
compilavel no Wokwi sem configuracao adicional.</p>
<p>A comparacao de versoes e <strong>numerica</strong>, nao textual: <code>"2.0"</code> vira
<code>200</code> e <code>"1.0"</code> vira <code>100</code>. Comparar strings quebraria em um futuro
<code>"10.0"</code>, que e textualmente menor que <code>"2.0"</code>.</p>

<h2>7. Tratamento de situacoes previstas</h2>
{tabela(["Situacao","Mensagem no Serial Monitor"], [
  ("Sem conexao Wi-Fi", "[OTA] ERRO: nao foi possivel conectar ao Wi-Fi."),
  ("Manifesto inacessivel", "[OTA] ERRO: manifesto inacessivel (HTTP <codigo>)."),
  ("Manifesto malformado", '[OTA] ERRO: manifesto sem os campos "version" e "url".'),
  ("Ja esta na versao mais recente", "[OTA] Firmware ja esta na versao mais recente."),
  ("Download ou gravacao falhou", "[OTA] ERRO <n> durante a atualizacao: <descricao>"),
])}
<p>Em todos os casos de falha o no <strong>continua medindo normalmente</strong> na versao atual.
Uma atualizacao que nao deu certo nunca derruba o servico de medicao.</p>

<h2>8. Testes obrigatorios</h2>
{tabela(["Teste","Condicao","Resultado esperado"], testes)}

<h2>9. Evidencias</h2>
<p><strong>Metodologia de validacao:</strong> a fila de compilacao gratuita do editor web do Wokwi
apresentou instabilidade recorrente ("Build Servers Busy" / "Failed to fetch") durante os testes.
Para nao depender dela, o grupo compilou os dois firmwares localmente com o <code>arduino-cli</code>
(o mesmo binario que e publicado no repositorio) e executou a simulacao completa pela
<strong>Wokwi CLI</strong> (<code>wokwi-cli</code>), que usa o mesmo motor de simulacao em nuvem do
editor web, valida o <code>diagram.json</code> e reproduz fielmente Wi-Fi, HTTPS e a gravacao OTA.
O log abaixo e a saida real e integral do Serial Monitor dessa execucao, sem edicao de conteudo
(apenas uma quebra de linha reconstituida onde a captura uniu duas mensagens). O projeto publicado
no link da secao 2 roda o mesmo codigo e pode ser reexecutado a qualquer momento clicando em Play.</p>
{secao_evidencias()}

<div class="quebra"></div>
<h2>10. Arquivo version.json</h2>
{bloco_codigo(ler("version.json"), numerar=False)}

<h2>11. Arquivo firmware_v2.bin</h2>
<p>O binario nao pode ser embutido em PDF. Ele esta publicado no repositorio e e o arquivo
efetivamente baixado pelo ESP32 durante a demonstracao. Sua identidade e comprovada pelo hash:</p>
{tabela(["Atributo","Valor"], [
  ("URL", f'<a href="{RAW}/firmware_v2.bin">{RAW}/firmware_v2.bin</a>'),
  ("Tamanho", f"{bintam:,} bytes".replace(",", ".")),
  ("SHA-256", binsha),
  ("Particao de destino", "app0/app1 (1.310.720 bytes) - esquema Default 4MB with spiffs"),
])}
<p>Para conferir apos o download: <code>shasum -a 256 firmware_v2.bin</code></p>

<div class="quebra"></div>
<h2>12. Codigo-fonte completo &mdash; Firmware 1.0</h2>
{bloco_codigo(ler("firmware_v1.ino"))}

<div class="quebra"></div>
<h2>13. Codigo-fonte completo &mdash; Firmware 2.0</h2>
{bloco_codigo(ler("firmware_v2.ino"))}

<div class="quebra"></div>
<h2>14. Instrucoes de execucao</h2>
<h3>No Wokwi (demonstracao)</h3>
<ol>
<li>Abrir o projeto Wokwi: <a href="{WOKWI}">{WOKWI}</a></li>
<li>Clicar em <strong>Play</strong>. O LED acende <strong>azul</strong> e o Serial Monitor mostra o FW 1.0.</li>
<li>Aguardar <strong>3 sessoes completas</strong> (~104 s). O no conecta ao Wi-Fi, consulta o
manifesto, detecta a versao 2.0 e inicia o download (LED <strong>magenta</strong>).</li>
<li>Apos a gravacao o ESP32 reinicia sozinho no <strong>Firmware 2.0</strong>, e o LED passa a
indicar o estado (<strong>verde</strong> NORMAL / <strong>vermelho</strong> ALERTA).</li>
<li>Para testar a histerese de forma deterministica, digitar no Serial Monitor:
<code>T1</code> (mediana &ge; 16), <code>T2</code> (mediana = 15), <code>T3</code> (mediana &le; 14).</li>
</ol>
<h3>Compilando localmente</h3>
{bloco_codigo('''brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls \\
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
./build.sh''', numerar=False)}
<p>O <code>build.sh</code> compila as duas versoes, gera o <code>firmware_v2.bin</code>, imprime
tamanho e SHA-256 e aborta se o binario nao couber na particao de aplicacao.</p>

<h3>Comandos de teste do Firmware 2.0</h3>
{tabela(["Comando","Leituras injetadas","Mediana","Resultado esperado"], [
  ("T1", "18 16 17 20 16", "17 cm", "-> ALERTA (vermelho)"),
  ("T2", "13 15 20 15 12", "15 cm", "mantem o estado anterior"),
  ("T3", "10 14 13 12 14", "13 cm", "-> NORMAL (verde)"),
])}
<p>A sequencia <code>T1 &rarr; T2 &rarr; T3 &rarr; T2</code> demonstra a histerese <strong>nos dois
sentidos</strong>: o <code>T2</code> mantem ALERTA quando vem depois do <code>T1</code>, e mantem
NORMAL quando vem depois do <code>T3</code>. E o mesmo valor de mediana produzindo estados
diferentes &mdash; que e exatamente o que histerese significa.</p>

</body></html>"""

with open(os.path.join(RAIZ, "relatorio.html"), "w", encoding="utf-8") as f:
    f.write(doc)
print("relatorio.html gerado")

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
if os.path.exists(CHROME):
    saida = os.path.join(RAIZ, "S2-CP02_Motiva_OTA_Relatorio.pdf")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={saida}", "file://" + os.path.join(RAIZ, "relatorio.html")],
                   check=True, capture_output=True)
    print("PDF gerado:", saida, os.path.getsize(saida), "bytes")
else:
    print("Chrome nao encontrado; apenas o HTML foi gerado.", file=sys.stderr)

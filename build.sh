#!/usr/bin/env bash
# ============================================================================
#  Compila os firmwares do projeto e gera o binario usado na atualizacao OTA.
#
#  O arduino-cli exige que o sketch esteja em uma pasta com o mesmo nome do
#  arquivo .ino. Como o enunciado pede os .ino na raiz do repositorio, a
#  compilacao acontece em uma pasta temporaria a partir do arquivo da raiz.
#  Assim existe uma unica fonte de verdade e o .bin nunca diverge do .ino.
#
#  Uso:  ./build.sh
# ============================================================================
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="esp32:esp32:esp32:PartitionScheme=default,FlashFreq=80,CPUFreq=240"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Tamanho maximo da particao de aplicacao (app0/app1) no esquema default 4MB
LIMITE_APP=1310720

compilar () {
  local nome="$1"
  echo "=== Compilando ${nome}.ino ==="
  mkdir -p "$TMP/$nome"
  cp "$RAIZ/$nome.ino" "$TMP/$nome/$nome.ino"
  arduino-cli compile --fqbn "$FQBN" --output-dir "$TMP/out_$nome" "$TMP/$nome"
  cp "$TMP/out_$nome/$nome.ino.bin" "$RAIZ/${nome}.bin"
  echo
}

compilar firmware_v1
compilar firmware_v2

# Apenas o binario da versao 2.0 e publicado para o OTA
rm -f "$RAIZ/firmware_v1.bin"

TAMANHO=$(stat -f%z "$RAIZ/firmware_v2.bin")
echo "============================================================"
echo "firmware_v2.bin : ${TAMANHO} bytes"
echo "limite da particao: ${LIMITE_APP} bytes"
echo "ocupacao: $(( TAMANHO * 100 / LIMITE_APP ))%"
echo "sha256: $(shasum -a 256 "$RAIZ/firmware_v2.bin" | cut -d' ' -f1)"
echo "============================================================"

if [ "$TAMANHO" -gt "$LIMITE_APP" ]; then
  echo "ERRO: o binario nao cabe na particao de aplicacao. OTA impossivel." >&2
  exit 1
fi
echo "OK: o binario cabe na particao de aplicacao."

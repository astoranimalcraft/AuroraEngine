#!/usr/bin/env bash
# Empacota a NexiumEngine para download.
#
# Gera dist/ com tres coisas: o repositorio inteiro num tarball, a biblioteca em
# arquivo unico (nexium.h) e os jogos de arquivo unico. Tambem escreve o
# dist/index.html que lista tudo.
#
# O conteudo e tirado do Git, nao do diretorio de trabalho, para que um arquivo
# de build esquecido por ai nao entre no pacote. Os arquivos ainda nao
# rastreados aparecem com --others, porque os artefatos gerados (as
# amalgamacoes dos jogos) sao justamente os que ninguem commitou ainda.
#
#   ./scripts/package.sh [diretorio-de-saida]     (padrao: dist)
set -u

cd "$(dirname "$0")/.."
OUT="${1:-dist}"
VERSION=$(sed -n 's/^project(NexiumEngine VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
VERSION="${VERSION:-0.1.0}"
TARBALL="NexiumEngine-src.tar.gz"

status=0
say()  { printf '%s\n' "$*"; }
fail() { printf 'erro: %s\n' "$*" >&2; status=1; }

# --------------------------------------------------------------- sanidade ---
# O pacote tem que sair de uma arvore que compila. Sem isso e so um zip de
# codigo quebrado.
if [ "${NEXIUM_SKIP_VERIFY:-0}" != "1" ]; then
    say "== verificando a arvore antes de empacotar =="
    if ! ./scripts/verify.sh > /tmp/nexium_package_verify.log 2>&1; then
        fail "verify.sh falhou; veja /tmp/nexium_package_verify.log"
        exit 1
    fi
    say "  ok      verify.sh"
fi

# ---------------------------------------------------------- amalgamacoes ---
say "== gerando as amalgamacoes =="
mkdir -p "$OUT"
if ! python3 tools/amalgamate_library.py "$OUT" >/dev/null; then
    fail "amalgamate_library.py falhou"
    exit 1
fi
say "  ok      $OUT/nexium.h"

# Os jogos de arquivo unico precisam existir antes de empacotar, senao o tarball
# sai com a engine antiga dentro deles. Sao gerados em games/ e copiados para a
# pasta de download, porque o index aponta para eles por nome.
for game in blaster heights rhythm prism; do
    if [ -f "games/nexium_${game}.cpp" ]; then
        python3 tools/amalgamate_game.py \
            "games/nexium_${game}_single.cpp" "games/nexium_${game}.cpp" >/dev/null \
            || fail "amalgamate_game.py falhou para $game"
        cp "games/nexium_${game}_single.cpp" "$OUT/"
    fi
done
say "  ok      games/*_single.cpp"

# --------------------------------------------------------------- tarball ---
say "== montando o tarball =="
mkdir -p "$OUT"

# Lista de arquivos: tudo que o Git conhece mais o que ainda nao foi adicionado,
# sem o que esta no .gitignore (dist/, build/, __pycache__).
# As duas pastas de download sao excluidas explicitamente: o tarball nao deve
# conter a si mesmo.
FILELIST=$(mktemp)
trap 'rm -f "$FILELIST"' EXIT
git ls-files --cached --others --exclude-standard \
    | grep -vE "^$OUT/" \
    > "$FILELIST" || true

if [ ! -s "$FILELIST" ]; then
    fail "a lista de arquivos saiu vazia"
    exit 1
fi

tar czf "$OUT/$TARBALL" --transform 's,^,NexiumEngine/,' -T "$FILELIST"
say "  ok      $OUT/$TARBALL ($(du -h "$OUT/$TARBALL" | cut -f1), $(wc -l < "$FILELIST") arquivos)"

# --------------------------------------------------------------- index -----
say "== escrevendo o index =="
size_of() { [ -f "$1" ] && du -h "$1" | cut -f1 || echo "?"; }

cat > "$OUT/index.html" <<HTML
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>NexiumEngine — downloads</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; padding: 48px 24px; background: #0d1117; color: #e6edf3;
         font: 16px/1.6 ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif; }
  main { max-width: 780px; margin: 0 auto; }
  h1 { font-size: 28px; margin: 0 0 4px; }
  h2 { font-size: 15px; text-transform: uppercase; letter-spacing: .06em;
       color: #8b949e; margin: 36px 0 10px; font-weight: 600; }
  p.sub { color: #8b949e; margin: 0 0 8px; }
  a.card { display: block; padding: 16px 20px; margin: 10px 0; text-decoration: none;
           color: inherit; background: #161b22; border: 1px solid #30363d; border-radius: 10px; }
  a.card:hover { border-color: #58a6ff; background: #1c2430; }
  .name { font-weight: 600; color: #58a6ff; }
  .desc { color: #8b949e; font-size: 14px; }
  .size { float: right; color: #6e7681; font-size: 13px; font-variant-numeric: tabular-nums; }
  code { background: #21262d; padding: 2px 6px; border-radius: 5px; font-size: 13px;
         display: inline-block; word-break: break-all; }
  pre { background: #161b22; border: 1px solid #30363d; border-radius: 10px;
        padding: 14px 16px; overflow-x: auto; font-size: 13px; }
</style>
</head>
<body>
<main>
  <h1>NexiumEngine ${VERSION}</h1>
  <p class="sub">Biblioteca 3D/2D em C++20 sobre DirectX 12 com HLSL. Windows.</p>

  <h2>Comece por aqui</h2>

  <a class="card" href="${TARBALL}" download>
    <span class="size">$(size_of "$OUT/$TARBALL")</span>
    <div class="name">${TARBALL}</div>
    <div class="desc">Repositório completo: engine, camada de plataforma, shaders,
      exemplos, jogos, testes e scripts. É o que você quer se for compilar com CMake.</div>
  </a>

  <a class="card" href="nexium.h" download>
    <span class="size">$(size_of "$OUT/nexium.h")</span>
    <div class="name">nexium.h</div>
    <div class="desc">A biblioteca inteira em um header, no formato stb. Defina
      <code>NEXIUM_IMPLEMENTATION</code> em uma unidade de tradução e inclua nas outras.</div>
  </a>

  <h2>Jogos de arquivo único</h2>
  <p class="sub">Cada um é o jogo <em>e</em> a engine num arquivo só. Compila sem
  <code>-I</code> e sem biblioteca.</p>

HTML

for game in rhythm prism blaster heights; do
    file="nexium_${game}_single.cpp"
    [ -f "$OUT/$file" ] || continue
    case "$game" in
        rhythm)  desc="Jogo de ritmo 2D em <code>n2d::</code>: notas em quatro pistas, combo, música sintetizada." ;;
        prism)   desc="Galeria de cinco shaders HLSL compilados em runtime e trocados por tecla." ;;
        blaster) desc="FPS de galeria de tiro: picking por raio, partículas, HUD e som gerado em memória." ;;
        heights) desc="Plataforma 3D em terceira pessoa: gravidade, colisão por eixo, plataformas móveis." ;;
    esac
    cat >> "$OUT/index.html" <<HTML
  <a class="card" href="$file" download>
    <span class="size">$(size_of "$OUT/$file")</span>
    <div class="name">$file</div>
    <div class="desc">$desc</div>
  </a>

HTML
done

cat >> "$OUT/index.html" <<'HTML'
  <h2>Como compilar</h2>
  <p class="sub">Um jogo de arquivo único, com MinGW-w64:</p>
  <pre>x86_64-w64-mingw32-g++ -std=c++20 -O2 nexium_rhythm_single.cpp \
    -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 \
    -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid \
    -o jogo.exe</pre>

  <p class="sub">O repositório completo, com CMake:</p>
  <pre>tar xzf NexiumEngine-src.tar.gz
cd NexiumEngine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release</pre>

  <p class="sub" style="margin-top:28px">
    Windows 10/11, CMake 3.20+ e um compilador C++20 (MSVC 2019+ ou MinGW-w64 GCC 13+).
  </p>
</main>
</body>
</html>
HTML
say "  ok      $OUT/index.html"

say ""
if [ "$status" -eq 0 ]; then
    say "pacote pronto em $OUT/"
    ls -la "$OUT" | sed 's/^/  /'
else
    say "o pacote saiu com problemas"
fi
exit "$status"

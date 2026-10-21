# NexiumEngine

Uma biblioteca 3D pequena e autossuficiente para **C++20**, construída direto sobre
**DirectX 12**, com **HLSL** de primeira classe. O modelo de programação é
*immediate-mode*, no espírito do raylib: funções curtas, verbos diretos, zero
boilerplate. A API, os nomes e a implementação são próprios — é "estilo raylib",
não um clone.

```cpp
#include <nexium/NexiumEngine.h>

int main() {
    n3d::Config config;
    config.title = "Meu jogo";
    if (!n3d::Init(config)) return 1;

    n3d::OrbitCamera orbit;

    while (!n3d::WindowShouldClose()) {
        n3d::UpdateOrbitCamera(orbit);

        n3d::BeginScene(orbit.ToCamera());
        n3d::DrawGrid();
        n3d::DrawCube({0, 0.5f, 0}, {1, 1, 1}, n3d::colors::Orange);
        n3d::EndScene();
    }

    n3d::Close();
    return 0;
}
```

Esse é o programa inteiro: janela, dispositivo D3D12, swap chain, câmera, luz,
desenho e sincronização de frames. Tudo isso são ~20 linhas de código de aplicação.

## Por que ela existe

O raylib é ótimo para prototipar, mas é OpenGL e não expõe o pipeline moderno. O
DirectX 12 sozinho é poderoso, mas exige centenas de linhas só para abrir uma
janela. NexiumEngine fica no meio: a ergonomia de desenhar do raylib, com um pipeline
D3D12 de verdade por baixo — PSOs, root signature, heaps de descritores, frames em
voo — e shaders HLSL que você pode escrever e recarregar em tempo real.

## Instalação

Só Windows. DirectX 12 não tem implementação em outro sistema.

**Requisitos:** Windows 10/11, CMake 3.20+, um compilador C++20 (MSVC 2019+ ou
MinGW-w64 GCC 13+) e o Windows SDK.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Isso gera `nexiumengine.lib` (ou `libnexiumengine.a`) e os sete exemplos em `build/`.

**Instalando como pacote.** O CMake exporta o alvo, então o projeto pode ser
instalado num prefixo e consumido com `find_package` de qualquer outro projeto:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/libs/nexium
cmake --build build --config Release
cmake --install build --config Release
```

Depois, no `CMakeLists.txt` do jogo:

```cmake
find_package(NexiumEngine 0.1 REQUIRED)
target_link_libraries(meu_jogo PRIVATE NexiumEngine::nexiumengine)
```

O alvo `NexiumEngine::nexiumengine` já carrega os diretórios de include e as
bibliotecas de link do DirectX, então não há mais nada a declarar. Para usar o
projeto direto da árvore de código, sem instalar:

```cmake
add_subdirectory(third_party/NexiumEngine)
target_link_libraries(meu_jogo PRIVATE nexiumengine)
```

Ou manualmente, com MinGW-w64:

```sh
x86_64-w64-mingw32-g++ -std=c++20 -Iinclude -Isrc meu_jogo.cpp \
    src/*.cpp src/internal/*.cpp \
    -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid \
    -luser32 -lgdi32 -lshcore -lxinput -lxaudio2_8 \
    -lmfplat -lmfreadwrite -lmfuuid -o meu_jogo.exe
```

## Como se usa

### A câmera é sua

A biblioteca nunca guarda uma câmera. Você monta uma `n3d::Camera3D` e passa para
`BeginScene()` a cada frame, o que mantém a lógica de câmera no seu código.

```cpp
n3d::Camera3D camera;
camera.position = {0, 3, -8};
camera.target   = {0, 1, 0};
camera.fovY     = 60.0f;

n3d::BeginScene(camera);
```

Para câmeras orbitais existe um controlador pronto, que já lê mouse e teclado:

```cpp
n3d::OrbitCamera orbit;
orbit.distance = 10.0f;

// por frame:
n3d::UpdateOrbitCamera(orbit);   // arrastar = olhar, roda = zoom, WASD = mover
n3d::BeginScene(orbit.ToCamera());
```

Para um jogo em primeira pessoa há `n3d::FpsCamera`, em que a própria câmera é o
olho: mouse olha, WASD anda e Espaço/Ctrl sobe e desce.

```cpp
n3d::FpsCamera fps;
fps.position = {0, 1.7f, 0};

// por frame:
n3d::UpdateFpsCamera(fps);
n3d::BeginScene(fps.ToCamera());
```

A `Camera3D` também traz utilitários de projeção e seleção: `WorldToScreen()` e
`RayThroughScreen()` convertem entre mundo e pixels (o `Ray` resultante alimenta
os testes de colisão), `GetCameraFrustum()` devolve um `Frustum` para culling, e
`LerpCamera()` mistura duas câmeras para transições suaves. Para impactos há
`CameraShake`, que decai sozinho:

```cpp
n3d::CameraShake shake;
shake.Add(0.8f);                 // no momento do impacto
shake.Update(n3d::GetFrameTime());
camera.position = camera.position + shake.Offset();
```

### Desenho immediate-mode

Tudo que você chama entre `BeginScene()` e `EndScene()` é **enfileirado**, não
enviado na hora. No fim do frame a biblioteca ordena e submete tudo de uma vez:

1. geometria opaca, ordenada por estado de pipeline (menos trocas de PSO)
2. geometria transparente, ordenada de trás para frente (blending depende da ordem)
3. linhas de debug, agrupadas em um único draw call
4. overlay 2D, ortográfico, sem teste de profundidade

Você escreve na ordem que quiser; a ordem de desenho correta é problema da
biblioteca.

```cpp
n3d::BeginScene(camera, n3d::colors::SkyBlue);

// primitivas de uma linha
n3d::DrawCube({0, 1, 0}, {1, 1, 1}, n3d::colors::Orange);
n3d::DrawSphere({3, 1, 0}, 1.0f, n3d::colors::Green);
n3d::DrawCylinder({-3, 1, 0}, 0.5f, 2.0f, n3d::colors::Blue);
n3d::DrawPlane({0, 0, 0}, {20, 20}, n3d::colors::DarkGray);
n3d::DrawCone({0, 2, 5}, 0.5f, 1.5f, n3d::colors::Red);

// malhas com transformação
const n3d::Mesh* mesh = n3d::GetSphereMesh(1.0f);
n3d::DrawMesh(mesh, {0, 2, 0}, n3d::Vec3{0, 45, 0}, n3d::Vec3{2, 2, 2});

// ou com uma matriz explícita
n3d::Mat4 world = n3d::Compose(position, rotation, scale);
n3d::DrawMeshEx(mesh, world, n3d::colors::White);

// material com textura e parâmetros
n3d::Texture* tex = n3d::LoadTextureFromFile("chao.png");
n3d::DrawMeshMaterial(mesh, {0, 0, 0}, tex, {}, {}, n3d::colors::White, 0.8f, 0.2f);

// debug
n3d::DrawGrid(10.0f, 20, n3d::colors::DarkGray);
n3d::DrawAxes({}, 2.0f);
n3d::DrawLine({0, 0, 0}, {0, 5, 0}, n3d::colors::Yellow);
n3d::DrawBounds(n3d::MeshBounds(mesh), n3d::colors::Green);

// overlay 2D, em pixels, origem no canto superior esquerdo
n3d::DrawScreenRect({10, 10}, {200, 40}, n3d::colors::Black);
n3d::DrawScreenLine({10, 60}, {210, 60}, n3d::colors::White, 2.0f);

n3d::EndScene();
```

### Shaders HLSL

A root signature é fixa, então um shader seu só precisa incluir `nexium.hlsli` e
definir `VSMain` / `PSMain`. Não há `.cso` para compilar junto nem passo de build: a
biblioteca compila o HLSL em tempo de execução.

```cpp
const char* fonte = R"HLSL(
#include "nexium.hlsli"

N3D_VertexOutput VSMain(N3D_VertexInput input) {
    return N3D_TransformVertex(input);
}

float4 PSMain(N3D_VertexOutput input, bool frente : SV_IsFrontFace) : SV_TARGET {
    return float4(input.color.rgb * input.uv.x, input.color.a);
}
)HLSL";

n3d::Shader* meu = n3d::LoadShaderFromMemory(fonte);

n3d::SetShader(meu);
n3d::DrawSphere({0, 1, 0}, 1.0f, n3d::colors::White);
n3d::SetShader(nullptr);   // volta ao shader padrão
```

O que a biblioteca te dá no HLSL:

```hlsl
cbuffer FrameConstants : register(b0)   // viewProj, cameraPosTime, lightDir, ambient
cbuffer ObjectConstants : register(b1)  // world, baseColor, uvScaleOffset, params, flags
Texture2D    albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);
```

Mais os helpers `N3D_TransformVertex()`, `N3D_Shade()` e `N3D_ShadePixel()`, e os
flags `N3D_FLAG_UNLIT`, `N3D_FLAG_TEXTURED` e `N3D_FLAG_TWOSIDED`.

Para editar um shader e ver o resultado sem recompilar o programa:

```cpp
n3d::Shader* s = n3d::LoadShaderFromFile("meu.hlsl", /*watchForChanges=*/true);
// o arquivo é recompilado automaticamente quando muda no disco
```

### Malhas e texturas próprias

```cpp
n3d::Vertex vertices[3] = { ... };
uint32_t indices[3] = {0, 1, 2};
n3d::Mesh* triangulo = n3d::CreateMesh(vertices, 3, indices, 3);

n3d::TextureDesc desc;
desc.width = 256;
desc.height = 256;
desc.pixels = meusPixelsRGBA8;
n3d::Texture* tex = n3d::LoadTexture(desc);

// ...

n3d::UnloadMesh(triangulo);
n3d::UnloadTexture(tex);
```

As primitivas `GetCubeMesh()`, `GetSphereMesh()` etc. são **cacheadas e
compartilhadas** — pedir a mesma duas vezes devolve a mesma malha, e
`UnloadMesh()` nelas não faz nada.

### 2D e texto

`Camera2D` transforma o mesmo desenho immediate-mode para um mundo 2D, com zoom e
rotação, e a conversão tela/mundo serve para picking com o mouse:

```cpp
n3d::Camera2D cam;
cam.offset = {GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
cam.target = jogador.posicao;
cam.zoom   = 2.0f;

n3d::BeginMode2D(cam);
n3d::DrawRectangle(0, 0, 64, 64, n3d::colors::Orange);
n3d::DrawCircleV(inimigo.posicao, 16.0f, n3d::colors::Red);
n3d::DrawPolygon({0, 0}, 6, 40.0f, 0.0f, n3d::colors::Cyan);
n3d::EndMode2D();

// mouse -> mundo
const n3d::Vec2 mundo = cam.ScreenToWorld(n3d::GetMousePosition(),
                                          GetScreenWidth(), GetScreenHeight());
```

Fora de um `BeginMode2D()` as mesmas funções desenham em pixels, com a origem no
canto superior esquerdo — é o que o overlay já usava.

O texto usa uma fonte de bitmap embutida, então não há arquivo de fonte para
distribuir. Tudo o que é texto tem variante `V` (posição em `Vec2`) e aceita
`printf` no `DrawTextFormat()`:

```cpp
n3d::DrawText("pontos: 120", 16, 16, 24.0f, n3d::colors::White);
n3d::DrawTextCentered("game over", GetScreenWidth() * 0.5f, 200.0f, 48.0f, n3d::colors::Red);
n3d::DrawTextFormat(16, 48, 20.0f, n3d::colors::Lime, "fps: %d", GetFps());

const n3d::Vec2 tamanho = n3d::MeasureText("pontos: 120", 24.0f);  // para centralizar você mesmo
```

### Áudio

`Sound` é decodificado na memória; `Music` faz streaming do disco e é alimentado
automaticamente a cada frame. O subsistema é best-effort: sem dispositivo de saída,
o carregamento devolve `nullptr` e o programa continua rodando.

```cpp
n3d::Sound* tiro = n3d::LoadSound("tiro.wav");
n3d::Music* trilha = n3d::LoadMusic("trilha.wav");

n3d::SetMusicVolume(trilha, 0.6f);
n3d::SetMusicLooping(trilha, true);
n3d::PlayMusic(trilha);

// no jogo:
n3d::PlaySound(tiro);                          // reinicia se já estiver tocando
n3d::PlaySoundMulti(passo, /*pitchRange=*/2.0f);  // sobrepõe cópias com pitch variado

n3d::UnloadMusic(trilha);
n3d::UnloadSound(tiro);
```

### Vídeo

`Video` abre um arquivo via Media Foundation e entrega o frame atual como textura.
`UpdateVideo()` avança o decoder uma vez por frame (o `EndScene()` já faz isso).

```cpp
n3d::Video* clip = n3d::LoadVideo("intro.mp4");
n3d::SetVideoLooping(clip, true);
n3d::PlayVideo(clip);

// no jogo, sobre o overlay 2D:
n3d::DrawVideoV(clip, {0, 0});
```

Dois detalhes que valem saber ao depurar um vídeo que não aparece:

- Se `LoadVideo()` devolve `nullptr`, o motivo está no log. Ele sai por
  `OutputDebugString` e por `stderr` **quando há um console anexado** — um build
  `-mwindows` não tem console, então não se vê nada. Chame
  `n3d::SetLogSink(...)` ou rode a partir de um terminal para ler a mensagem, que
  distingue "não abriu o arquivo" de "não achou um stream de vídeo decodificável".
- O caminho é relativo ao **diretório de trabalho** do processo, não ao `.exe`. Um
  `.mp4` ao lado do executável só é encontrado se o diretório de trabalho for o
  mesmo; usar caminho absoluto ou ajustar o diretório de trabalho evita a confusão.

`GetVideoTexture()` devolve uma textura comum, do tamanho exato do vídeo
(`GetVideoWidth()` x `GetVideoHeight()`), então ela pode ser desenhada como
qualquer outra textura. A conversão de BGRA (o que o Media Foundation entrega) para
RGBA (o que o engine amostra) e a cópia para a GPU já estão feitas; nada disso
aparece para quem chama.

### Modelos

`LoadModel()` lê OBJ/MTL sem dependência extra (com Assimp opcional para outros
formatos) e devolve um modelo com várias partes, cada uma com o seu material:

```cpp
n3d::Model* robo = n3d::LoadModel("robo.obj");

n3d::DrawModel(robo, {0, 0, 0}, n3d::Vec3{0, 90, 0}, n3d::Vec3{1, 1, 1});
n3d::DrawModelEx(robo, n3d::Compose(pos, rot, escala), n3d::colors::White);

n3d::UnloadModel(robo);
```

### Storage: a gaveta de dados

Os caminhos de arquivo de um jogo costumam ficar espalhados — uma música aqui, um
modelo ali — e acabam duplicados por vários arquivos sem um lugar único para olhar.
Uma `Storage` junta tudo: você declara a gaveta uma vez, dá um nome curto a cada
arquivo, e daí em diante usa o nome, nunca o caminho.

```cpp
// A forma simples: uma gaveta montada em tempo de execução.
n3d::Storage Game = {
    {"music", "assets/overworld.ogg"},
    {"hero",  "assets/hero.obj"},
    {"sky",   "assets/sky.hdr"},
};

Game["music"].AsMusic();   // carrega no primeiro uso e guarda
Game["hero"].AsModel();
Game.Count();              // quantos arquivos a gaveta tem
Game.SetBaseDirectory("assets");   // prefixo para todos os caminhos relativos
Game.UnloadAll();
```

A forma nomeada transforma cada arquivo em membro estático, então o compilador
confere o nome e o editor autocompleta. A lista é definida uma vez com uma macro
X, e a gaveta é construída a partir dela:

```cpp
#define GAME_FILES(X)                 \
    X(music, "assets/overworld.ogg")  \
    X(hero,  "assets/hero.obj")       \
    X(sky,   "assets/sky.hdr")

// dentro de namespace n3d { ... } — é isso que faz n3d::Game::music existir
N3D_DEFINE_STORAGE(Game, GAME_FILES)

n3d::Game::music.AsMusic();     // o arquivo individual
n3d::Game::Drawer().LoadAll();  // a gaveta inteira
n3d::Game::Get("hero");         // procura por nome
```

O carregamento é preguiçoso e cacheado: o primeiro `As*()` de um arquivo o carrega,
as chamadas seguintes devolvem o mesmo handle, e `Unload()`/`UnloadAll()` liberam. Um
arquivo que falha devolve `nullptr` (com aviso no log) em vez de lançar exceção,
como o resto da biblioteca. `AsTexture()`, `AsSound()`, `AsMusic()`, `AsVideo()`,
`AsModel()`, `AsShader()`, `AsText()` e `AsBytes()` escolhem o carregador pelo tipo
do arquivo — a extensão só dá a dica inicial.

Um manifesto de texto também alimenta uma gaveta, para quem preferir editar sem
recompilar:

```cpp
n3d::LoadStorageManifest(Game, "assets/manifest.txt");   // uma linha "nome = caminho"
```

### n2d: a mesma biblioteca, grafia 2D

A engine tem um namespace, `n3d`, porque tem uma API: as chamadas 2D
(`DrawRectangle`, `BeginMode2D`, ...) e as 3D (`DrawCube`, `BeginScene`, ...) ficam
lado a lado e um jogo mistura as duas à vontade. Em vez de duplicar dois mil
símbolos sob um segundo nome — que se separariam na primeira mudança —, `n2d` é um
alias:

```cpp
#include <nexium/n2d.h>

n2d::Storage Sprites = {{"hero", "assets/hero.png"}};
n2d::BeginMode2D(camera2d);
n2d::DrawTexture(Sprites["hero"].AsTexture(), 10, 10);
n2d::EndMode2D();
```

Tudo o que está em `n3d` — inclusive uma gaveta declarada com
`N3D_DEFINE_STORAGE` dentro de `namespace n3d` — é alcançável como `n2d::`. A
convenção é de intenção, não de capacidade: escreva `n2d::` num jogo 2D para o
leitor saber que a câmera é 2D e o mundo é a tela, e `n3d::` num jogo 3D. Os dois
compilam exatamente para o mesmo código.

### Estado de renderização

```cpp
n3d::SetBlendMode(n3d::BlendMode::AlphaBlend);   // Opaque, AlphaBlend, Additive
n3d::SetCullMode(n3d::CullMode::Back);           // Back, Front, None
n3d::SetDepthTest(n3d::DepthTest::Less);         // Less, LessEqual, Always

n3d::SetLightDirection({-0.5f, -1.0f, -0.4f});
n3d::SetAmbient(n3d::colors::White, 0.3f);
```

### Sombras

Uma shadow map direcional, desligada por padrão. Quando ligada, `EndScene()`
renderiza a geometria opaca mais uma vez do ponto de vista da luz, para um alvo só
de profundidade, e a passagem principal o amostra com PCF. A sombra acompanha a
câmera, então a resolução continua útil num mundo grande.

```cpp
n3d::SetShadowsEnabled(true);
n3d::SetShadowResolution(2048);   // potência de dois; 1024 e 2048 são a faixa útil
n3d::SetShadowDistance(40.0f);    // meio-lado da caixa ao redor da câmera
n3d::SetShadowBias(0.0015f, 0.02f);   // profundidade, mundo: suba se aparecer acne

// Uma malha que não deve projetar sombra (o chão, o céu):
n3d::SetMeshCastsShadow(meuChao, false);
```

O custo é uma passagem de geometria a mais. `SetMeshCastsShadow()` marca a malha,
então toda chamada de desenho dela concorda sem o programa precisar controlar
estado por draw.

### Ambiente e HDRI

Um céu equirretangular (2:1) desenhado como um cubo em volta da câmera. A textura
pode ser um PNG comum, mas só o caminho HDR preserva valores acima de 1 — e só ele
serve como luz.

```cpp
// Um radiance map de verdade: o sol continua sendo 24x o branco.
n3d::Texture* ceu = n3d::LoadHDRFromFile("por_do_sol.hdr");
n3d::SetSky(ceu);

// Faz o céu iluminar a cena: a cor média vira o ambiente, e a direção mais
// brilhante vira a luz direcional. É a versão barata de IBL: uma passagem sobre a
// imagem, sem mapa de irradiância e sem integrar BRDF.
n3d::UpdateLightingFromSky(0.55f);

n3d::BeginScene(camera);
n3d::DrawSky();   // antes do resto: o depth buffer oclui o que vem depois
n3d::DrawCube({0, 1, 0}, {1, 1, 1}, n3d::colors::Orange);
n3d::EndScene();
```

Para gerar o mapa em código em vez de carregar um arquivo, use
`LoadTextureFromFloats()`, que aceita RGBA linear em `float` e o guarda como
`RGBA16F` na GPU:

```cpp
std::vector<float> pixels = GerarMeuCeu(width, height);   // valores podem passar de 1
n3d::Texture* ceu = n3d::LoadTextureFromFloats(pixels.data(), width, height);
```

`LoadTexture()` aceita `TextureDesc::format = n3d::TextureFormat::RGBA16F` para o
mesmo efeito a partir de half-floats já prontos, e `TexturePixelFormat()` diz qual
formato uma textura tem. O formato `RGBA16F` carrega 5 bits de expoente — que é o
ponto inteiro do HDR — e 10 de mantissa, mais do que os 8 bits de sRGB jamais
tiveram.

### Overlay sobre uma cena 3D

`BeginSceneOverlay()` mantém o que já está no buffer de cor (o de profundidade é
limpo), que é como se desenha uma segunda passada por cima:

```cpp
n3d::BeginScene(camera);
n3d::DrawCube({0, 0, 0}, {1, 1, 1});
n3d::EndScene();

n3d::BeginSceneOverlay(camera);
n3d::DrawScreenRect({10, 10}, {200, 40}, n3d::colors::Black);
n3d::EndScene();
```

### Diagnóstico

```cpp
n3d::RenderStats stats = n3d::GetRenderStats();
n3d::LogInfo("%u draw calls, %u triângulos, %u trocas de estado",
             stats.drawCalls, stats.triangles, stats.stateChanges);
```

Para depurar problemas de pipeline, ative a camada de depuração do D3D12 **antes**
do `Init()`:

```cpp
n3d::SetDebugLayerEnabled(true);
```

Erros são reportados por `n3d::LogError()` e por retorno `false`/`nullptr`. A
biblioteca nunca lança exceção pela sua fronteira pública.

## Arquitetura

```
include/nexium/       headers públicos, um por subsistema
  NexiumEngine.h            janela, ciclo de vida, tempo, luzes, estatísticas
  draw.h              a API immediate-mode de desenho (3D, 2D e texto)
  camera.h            Camera3D, OrbitCamera
  mesh.h              Vertex, Mesh, primitivas
  texture.h           texturas (arquivo ou memória)
  shader.h            shaders HLSL e estado de pipeline
  audio.h             Sound, Music (XAudio2)
  video.h             Video (Media Foundation)
  model.h             Model, carregado de OBJ/MTL
  storage.h           Storage, a gaveta de dados por nome
  input.h             teclado e mouse
  math.h              vetores, matrizes, quatérnios, bounds, raios
  color.h             Color e a paleta
  log.h               log com nível
  n2d.h               alias: n2d é n3d, a grafia 2D da mesma API

src/                  implementação
  internal/
    platform.h             a interface de plataforma (janela, eventos, monitores)
    platform_win32.cpp     o backend Win32: o único que inclui <windows.h>
    window.h/.cpp          estado de entrada e relógio, sobre o backend
    gpu.h/.cpp             dispositivo, swap chain, heaps, fences
    renderer.h/.cpp        root signature, PSOs, filas de desenho
    …                      o resto do que só existe no Windows

tools/                geradores (amalgamação)
  amalgamate_library.py  gera dist/nexium.h, a biblioteca em um header
  amalgamate_game.py     gera games/*_single.cpp, um jogo em um arquivo
cmake/                toolchain de cross-compile e o template do Config.cmake
shaders/              HLSL embutido no binário, legível e editável
examples/             sete programas completos
tests/                testes de math, geometria e câmera 2D, rodam nativamente
```

### Decisões que valem saber

**Coordenadas.** Sistema destro (right-handed), profundidade em `[0, 1]`
(convenção nativa do D3D), matrizes **coluna-maior** com vetores coluna. Ou seja,
`M * v` em C++ é exatamente `mul(M, v)` em HLSL com empacotamento padrão — os dois
lados nunca precisam transpor.

**Constantes por root constants.** `b0` e `b1` são enviados como *root constants*,
não CBVs. É o caminho de binding mais barato do D3D12 e dispensa buffer de upload
por frame.

**Frames em voo.** A biblioteca mantém vários frames em voo e usa fence por slot
para não reescrever um buffer que a GPU ainda está lendo. `EndScene()` apresenta e
rotaciona o slot.

**Shaders embutidos.** `tools/embed_shaders.py` transforma `shaders/*.hlsl` em
literais de string compilados no binário, então um programa NexiumEngine roda sem
nenhum arquivo de shader ao lado do executável. Os `.hlsl` continuam no disco por
serem a fonte legível — e o que você copia para escrever um shader próprio.

## Exemplos

| Exemplo | O que mostra |
| --- | --- |
| `01_hello_cube.cpp` | cubo girando, grid, linhas, esfera translúcida, overlay 2D |
| `02_custom_shader.cpp` | shader HLSL próprio, gradiente, troca de shader por draw |
| `03_mesh_and_texture.cpp` | malha procedural, textura gerada em código, material |
| `04_2d.cpp` | câmera 2D, formas, sprites e HUD com texto |
| `05_audio_model.cpp` | modelo OBJ, música e efeito sonoro, `BeginShader()` |
| `06_video.cpp` | reprodução de vídeo em um quad com `GetVideoTexture()` |
| `07_skybox_hdri.cpp` | ambiente HDR em float, `SetSky()`, `DrawSky()`, `UpdateLightingFromSky()` |

## Jogos

Aplicações completas construídas só com a API pública — o melhor exemplo de como um
laço de jogo de verdade se parece em cima da NexiumEngine.

| Jogo | O que mostra |
| --- | --- |
| `games/nexium_blaster.cpp` | galeria de tiro em primeira pessoa: `FpsCamera`, picking por raio, colisão esfera-raio, HUD 2D, partículas, tracers, camera shake, recuo e som sintetizado |
| `games/nexium_heights.cpp` | plataforma 3D em terceira pessoa: gravidade, colisão AABB resolvida por eixo, plataformas móveis que carregam o jogador, câmera de perseguição, malha toroidal girando e coleta com condição de vitória |
| `games/nexium_rhythm.cpp` | jogo de ritmo 2D escrito em `n2d::`: notas caindo em quatro pistas, julgamento por proximidade, combo, screen shake e música sintetizada — os caminhos ficam numa gaveta `n2d::Audio` |
| `games/nexium_prism.cpp` | galeria de shaders HLSL: cinco shaders próprios (plasma, fresnel, holograma, grid, normais) compilados em runtime e trocados por tecla, com a cena e as fontes nomeadas numa `n3d::Assets` |

**Nexium Blaster** é jogável de ponta a ponta sem nenhum arquivo de asset: os sons
são gerados como WAV em memória no início e carregados com `LoadSound()`. São 60
segundos por rodada, seis alvos flutuantes, pente de 12 tiros, combo e placar.
Alvos mais distantes valem mais pontos.

**Nexium Heights** é o outro lado da biblioteca: um percurso de plataformas
flutuantes subindo no céu. `WASD` anda (relativo à câmera), espaço pula — com
*coyote time* e *jump buffer*, então pular na beirada ou um pouco antes de aterrissar
funciona. Três plataformas se movem e levam o jogador junto; orbes vermelhos giram em
volta das mais altas. Junte as 16 gemas em até 90 segundos, com 3 vidas.

**Nexium Rhythm** é o jogo 2D, escrito inteiro em `n2d::` para mostrar a grafia de
intenção. Notas caem em quatro pistas (`D F J K`); apertar a tecla perto da linha vale
pontos e combo, errar zera o combo. A trilha e os efeitos são gerados como WAV no
início e carregados *pela gaveta* `n2d::Audio`, que é o mesmo caminho de um `.wav`
real.

**Nexium Prism** é a galeria de shaders: cinco HLSL próprios escritos como strings,
compilados em runtime e trocados com `1`–`5` ou as setas. Cada um é uma técnica
diferente — plasma animado, fresnel, holograma com scanlines, grid com brilho e
visualização de normais —, e todos usam o mesmo root signature fixo do motor.

```sh
# Windows (MSVC ou MinGW), a partir da raiz do repositório:
cmake -S . -B build && cmake --build build --config Release
build/nexium_blaster.exe
build/nexium_heights.exe
build/nexium_rhythm.exe
build/nexium_prism.exe
```

Controles (Blaster): `WASD` anda, mouse olha, botão esquerdo atira, `R` recarrega,
`P` pausa, `ESC` sai.

Controles (Heights): `WASD` anda, espaço pula, arraste o mouse (ou `Q`/`E`) para
girar a câmera, roda do mouse dá zoom, `R` reinicia, `ESC` sai.

Controles (Rhythm): `D F J K` bate nas quatro pistas, espaço começa, `ESC` sai.

Controles (Prism): `1`–`5` ou as setas trocam de shader, arraste o mouse orbita,
roda do mouse dá zoom, `ESC` sai.

### Versão de arquivo único

Há duas formas de arquivo único, e elas respondem a perguntas diferentes.

**A biblioteca inteira em um header** — `dist/nexium.h`, gerado por
`tools/amalgamate_library.py`. É o formato stb: a API inteira em cima, a
implementação embaixo atrás de `NEXIUM_IMPLEMENTATION`. Em **uma** unidade de
tradução:

```cpp
#define NEXIUM_IMPLEMENTATION
#include "nexium.h"
```

E nas demais, só `#include "nexium.h"`. Se preferir não espalhar o define pelo
projeto, compile junto o `dist/nexium_impl.cpp`, que faz isso por você:

```sh
python3 tools/amalgamate_library.py dist

# modo 1 — o define no seu próprio arquivo:
x86_64-w64-mingw32-g++ -std=c++20 -O2 meu_jogo.cpp \
    -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 \
    -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid \
    -o meu_jogo.exe

# modo 2 — o define fica no nexium_impl.cpp:
x86_64-w64-mingw32-g++ -std=c++20 -O2 meu_jogo.cpp nexium_impl.cpp \
    -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 \
    -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid \
    -o meu_jogo.exe
```

**Um jogo em um arquivo** — os `games/*_single.cpp` são os jogos com a engine
inteira dentro, para levar para outro lugar ou para ver o motor e o jogo juntos.
São gerados, não editados à mão (o segundo argumento escolhe o jogo; sem ele, gera
o Blaster):

```sh
python3 tools/amalgamate_game.py games/nexium_blaster_single.cpp
python3 tools/amalgamate_game.py games/nexium_heights_single.cpp games/nexium_heights.cpp
python3 tools/amalgamate_game.py games/nexium_rhythm_single.cpp games/nexium_rhythm.cpp
python3 tools/amalgamate_game.py games/nexium_prism_single.cpp  games/nexium_prism.cpp

# compila sozinho (MinGW-w64):
x86_64-w64-mingw32-g++ -std=c++20 -O2 games/nexium_blaster_single.cpp \
    -ld3d12 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -luser32 \
    -lgdi32 -lshcore -lxinput -lxaudio2_8 -lmfplat -lmfreadwrite -lmfuuid \
    -o nexium_blaster.exe
```

Para os jogos normais, prefira `games/nexium_blaster.cpp` e
`games/nexium_heights.cpp`, que linkam a biblioteca e portanto compilam muito mais
rápido.

### Camada de plataforma

A janela não fala com o Windows diretamente. `src/internal/platform.h` declara uma
interface — `PlatformBackend` — com tudo que a engine precisa do sistema
operacional: criar a janela, bombear mensagens, consultar monitores, mexer no
cursor e no clipboard. `src/internal/platform_win32.cpp` é a implementação para
Win32; é o único arquivo que inclui `<windows.h>` para esse fim.

O modelo de eventos segue o do SDL3: o backend traduz as mensagens nativas para
valores `PlatformEvent` e empilha numa fila; a camada de janela drena a fila e
transforma em estado de entrada. O backend nunca escreve direto nos arrays de
entrada, então um backend novo não tem como dessincronizá-los.

**O que isso torna possível, e o que não torna.** A camada de janela é mesmo
portável — outra implementação de `PlatformBackend` cobre Linux, macOS ou console
sem que nada acima mude. O renderizador, não. DirectX 12 é Windows e Xbox, e
`src/internal/gpu.cpp` recebe o `HWND` do backend e faz o cast: é a única linha do
motor que assume Win32, e ela assume porque D3D12 já assumiu. Para outra
plataforma seria preciso um renderizador que não fosse D3D12 (Vulkan, Metal, o
próprio driver da plataforma). A separação deixa o trabalho possível e bem
delimitado; ela não o faz sozinho.

`n3d::GetPlatformCapabilities()` responde o que o backend atual consegue fazer, e
`n3d::GetPlatformName()` diz qual está ativo. Um jogo que rode em mais de um
backend consulta as capacidades em vez de supor que o recurso existe.

### Janela e cursor

Além do que já havia (`SetWindowTitle`, `SetFullscreen`, `MinimizeWindow`, …):

```cpp
n3d::ShowWindow();              n3d::HideWindow();     // sem recriar a janela
n3d::IsWindowVisible();         n3d::IsWindowMaximizedChanged();
n3d::GetCurrentMonitor();                              // índice do monitor atual
n3d::GetMonitorName();          n3d::GetWindowDpiScale();

n3d::SetMouseCursorFromFile("cursors/mira.cur");       // .cur ou .ani
n3d::SetRawMouseInput(true);                           // movimento sem aceleração
n3d::SetMouseRect({0, 0}, {640, 480});                 // prende o mouse a um retângulo
n3d::GetMousePositionNormalized();                     // posição em [0, 1]
```

`SetRawMouseInput` é o modo certo para câmera de mouse-look: o movimento chega na
resolução do dispositivo, sem aceleração do cursor e sem parar na borda da tela.
`SetMouseRect` serve para letterbox e split-screen, onde o ponteiro não deve
entrar nas faixas pretas. `GetPlatformCapabilities().rawMouse` diz se o backend
tem raw input antes de você contar com ele.

## Build de desenvolvimento

O sandbox de desenvolvimento é Linux. DirectX 12 é Windows-only, então os programas
**não rodam aqui** — mas dá para type-check e linkar tudo, que é o que pega erro de
API e de ABI sem precisar de GPU:

```sh
./scripts/verify.sh              # cross-compila a biblioteca, exemplos e a API pública
./scripts/verify.sh --warnings   # e trata warning como erro
```

O que **roda de verdade** no Linux é math, cor e geometria, porque não dependem do
Windows:

```sh
./scripts/test-math.sh
```

Ambos os scripts exigem `x86_64-w64-mingw32-g++` (MinGW-w64) e um `g++` nativo. O
build com CMake no Windows é o caminho normal; esses scripts existem para o ciclo
de desenvolvimento aqui.

O `CMakeLists.txt` também pode ser validado a partir do Linux, com um toolchain de
cross-compile:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
cmake --build build
```

## Estado atual

A biblioteca está em desenvolvimento inicial. O que já funciona:

- janela Win32, entrada (teclado, mouse, captura, wheel) e tempo
- dispositivo D3D12, swap chain, heaps, fences, frames em voo
- root signature fixa, cache de PSOs, blend/cull/depth por estado
- compilação de HLSL em runtime, com hot reload opcional
- malhas (estáticas e primitivas) e texturas (WIC, HDR e procedurais, com mips)
- formatos de textura RGBA8 e RGBA16F (float), com geração de mips em ponto flutuante
- renderer immediate-mode com ordenação de transparência e batching de linhas
- sombras: shadow map direcional com PCF, opcional e ligado por `SetShadowsEnabled()`
- ambiente: céu equirretangular em HDR, com `SetSky()`/`DrawSky()` e luz derivada do céu
- desenho 2D: câmera com zoom/rotação, formas, sprites e texto com fonte embutida
- áudio: `Sound` (XAudio2) e `Music` com streaming
- vídeo via Media Foundation, entregue como textura
- modelos OBJ/MTL com materiais (Assimp opcional)
- câmera livre e controlador orbital
- controlador em primeira pessoa, frustum culling, picking por raio e camera shake
- janela: posição, monitores, DPI, opacidade, ícone e área de transferência
- camada de plataforma plugável: `PlatformBackend` + fila de eventos no estilo SDL3,
  com o backend Win32 como implementação real (raw input, cursores `.cur`/`.ani`,
  DPI por monitor)
- instalável: CMake `EXPORT` + `NexiumEngineConfig.cmake`, consumível com
  `find_package(NexiumEngine)` de outro projeto
- a biblioteca inteira em um header (`dist/nexium.h`), no formato stb
- colisões 2D/3D puras (esferas, caixas, retângulos, raios, segmentos) em `math.h`
- mais de 2100 checagens de teste em math, geometria, câmera e o decodificador HDR

O que ainda **não** existe: pós-processamento, MSAA configurável, render para
textura, cubemaps, mapa de irradiância (o `UpdateLightingFromSky()` é uma
aproximação, não IBL de verdade), animação/esqueleto e física. A ideia é manter o
núcleo pequeno; essas coisas cabem em cima dele.

## Licença

MIT.

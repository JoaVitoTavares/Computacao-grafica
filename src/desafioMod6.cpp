// =============================================================
// Desafio Modulo 6 — Trajetorias Parametricas
//
// Continuacao do desafio do modulo 5: mantem a camera em 1a pessoa
// com iluminacao Phong multiluz e adiciona trajetorias parametricas
// por objeto (interpolacao linear ciclica entre pontos de controle),
// conforme o material do M6 (Curvas Parametricas).
//
// Camera sintetica com projecao perspectiva e navegacao em 1a
// pessoa, encapsulada em uma classe Camera com metodos Mover
// (WASD/QE) e Rotacionar (mouse), conforme o material do M5.
//
// Incorpora as funcionalidades de iluminacao da Vivencial 2 (M4):
//   - Phong com 3 luzes coloridas posicionadas para revelar
//     diferentes faces conforme a camera navega:
//       * Principal (frente, +Z): ilumina o lado de partida da camera
//       * Preenchimento (lateral, +X): ilumina o lado direito
//       * Fundo (atras, -Z): ilumina apenas as costas dos objetos
//   - Toggle individual de cada luz (teclas 1, 2, 3)
//   - Coeficientes Ka/Kd/Ks (vec3) e Ns lidos do .MTL
//
// Trajetorias parametricas (M6 — Curvas Parametricas):
//   - Cada objeto pode ter uma trajetoria ciclica formada por uma
//     lista de pontos de controle (P0, P1, ..., Pn-1)
//   - Interpolacao linear entre pontos: P(t) = (1-t)*Pi + t*Pi+1
//     (curva de grau 1; a cubica fica para a Atividade Vivencial)
//   - Os pontos podem ser carregados/salvos em assets/trajetorias.txt
//     ou adicionados em tempo de execucao pela posicao da camera
//
// Controles:
//   W A S D        — andar (frente/tras/esquerda/direita)
//   Q / E          — descer / subir
//   Mouse          — olhar ao redor (yaw / pitch)
//   Scroll         — zoom (altera FOV)
//   1 / 2 / 3      — liga/desliga luz principal / preenchimento / fundo
//   M              — alterna solido / wireframe
//   TAB            — seleciona o proximo objeto (para editar trajetoria)
//   N              — modo edicao on/off (cruz central fica roxa quando on)
//   P              — adiciona ponto (so funciona com modo edicao ATIVO)
//                    na posicao atual da camera
//   C              — limpa a trajetoria do objeto selecionado
//   T              — pausa / retoma a animacao do selecionado
//   L              — recarrega assets/trajetorias.txt
//   O              — salva as trajetorias em assets/trajetorias.txt
//   ESC            — fechar
// =============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace std;
using namespace glm;

// -------------------------------------------------------------
// Constantes
// -------------------------------------------------------------

const int WIDTH  = 1000;
const int HEIGHT = 700;

// -------------------------------------------------------------
// Classe Camera — encapsula posicao, orientacao e operacoes de
// movimentacao e rotacao em primeira pessoa.
// -------------------------------------------------------------

enum class CameraMovement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

class Camera {
public:
    vec3  Position;
    vec3  Front;
    vec3  Up;
    vec3  Right;
    vec3  WorldUp;

    float Yaw;
    float Pitch;

    float MovementSpeed;
    float MouseSensitivity;
    float Fov;

    Camera(vec3 position   = vec3(0.0f, 1.5f, 7.0f),
           vec3 worldUp    = vec3(0.0f, 1.0f, 0.0f),
           float yaw       = -90.0f,
           float pitch     = 0.0f)
        : Position(position),
          Front(vec3(0.0f, 0.0f, -1.0f)),
          WorldUp(worldUp),
          Yaw(yaw),
          Pitch(pitch),
          MovementSpeed(3.0f),
          MouseSensitivity(0.1f),
          Fov(45.0f)
    {
        updateCameraVectors();
    }

    mat4 getViewMatrix() const {
        return lookAt(Position, Position + Front, Up);
    }

    mat4 getProjectionMatrix(float aspect) const {
        return perspective(radians(Fov), aspect, 0.1f, 100.0f);
    }

    // Move a camera ao longo dos seus eixos locais (1a pessoa).
    void mover(CameraMovement dir, float deltaTime) {
        float v = MovementSpeed * deltaTime;
        switch (dir) {
            case CameraMovement::FORWARD:  Position += Front * v; break;
            case CameraMovement::BACKWARD: Position -= Front * v; break;
            case CameraMovement::LEFT:     Position -= Right * v; break;
            case CameraMovement::RIGHT:    Position += Right * v; break;
            case CameraMovement::UP:       Position += WorldUp * v; break;
            case CameraMovement::DOWN:     Position -= WorldUp * v; break;
        }
    }

    // Rotaciona a camera a partir do deslocamento do mouse (em pixels).
    void rotacionar(float xoffset, float yoffset, bool constrainPitch = true) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        if (constrainPitch) {
            if (Pitch >  89.0f) Pitch =  89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;
        }

        updateCameraVectors();
    }

    // Zoom in/out alterando o FOV.
    void zoom(float yoffset) {
        Fov -= yoffset;
        if (Fov < 1.0f)  Fov = 1.0f;
        if (Fov > 60.0f) Fov = 60.0f;
    }

private:
    void updateCameraVectors() {
        vec3 f;
        f.x = cos(radians(Yaw)) * cos(radians(Pitch));
        f.y = sin(radians(Pitch));
        f.z = sin(radians(Yaw)) * cos(radians(Pitch));
        Front = normalize(f);
        Right = normalize(cross(Front, WorldUp));
        Up    = normalize(cross(Right, Front));
    }
};

// -------------------------------------------------------------
// Estruturas — Material (MTL), Objeto3D, Luz
// -------------------------------------------------------------

struct MaterialMTL {
    vec3   Ka    = vec3(0.2f);
    vec3   Kd    = vec3(0.8f);
    vec3   Ks    = vec3(0.5f);
    float  Ns    = 32.0f;
    string mapKd;
};

// Trajetoria — lista de pontos de controle percorrida ciclicamente
// por interpolacao linear (segmento por segmento). Cada objeto da
// cena pode ter a sua propria trajetoria, com velocidade ajustavel.
struct Trajetoria {
    vector<vec3> pontos;            // pontos de controle (em ordem)
    int          segmento  = 0;     // indice do segmento atual
    float        t         = 0.0f;  // parametro [0,1] no segmento
    float        velocidade = 1.5f; // unidades por segundo
    bool         ativa     = true;
};

struct Objeto3D {
    GLuint VAO        = 0;
    int    nVertices  = 0;
    string nome;
    vec3   posicao    = vec3(0.0f);
    vec3   rotacao    = vec3(0.0f);
    vec3   escala     = vec3(1.0f);
    vec3   Ka         = vec3(0.2f);
    vec3   Kd         = vec3(0.8f);
    vec3   Ks         = vec3(0.5f);
    float  brilho     = 32.0f;
    GLuint texID      = 0;
    bool   temTextura = false;
    Trajetoria trajetoria;
};

struct Luz {
    vec3 pos;
    vec3 color;
    bool ativa = true;
};

// Tres luzes posicionadas em torno da cena para que cada uma ilumine
// apenas o seu lado — assim, conforme a camera navega, e possivel ver
// quais faces estao acesas e quais ficam no escuro.
//   - Principal:     vem da frente (lado +Z, mesmo lado da camera inicial)
//   - Preenchimento: vem da lateral (lado +X)
//   - Fundo:         vem de tras (lado -Z, oposto a camera inicial)
Luz luzes[3] = {
    { vec3( 0.0f,  3.0f,  6.0f), vec3(1.0f, 1.0f, 0.95f), true }, // principal (frente, +Z)
    { vec3( 7.0f,  2.5f,  0.0f), vec3(0.4f, 0.6f, 1.0f),  true }, // preenchimento (lateral, +X)
    { vec3( 0.0f,  3.0f, -8.0f), vec3(1.0f, 0.8f, 0.5f),  true }, // fundo (atras, -Z)
};

vector<Objeto3D> objetos;
int              objetoSelecionado = 0;     // indice do objeto editavel
bool             modoWireframe     = false;

// Caminho do arquivo de configuracao de trajetorias
const string ARQUIVO_TRAJETORIAS = "assets/trajetorias.txt";

// Recursos do shader de visualizacao da trajetoria (pontos + linhas).
// Sao globais para serem destruidos no shutdown.
GLuint programaTraj = 0;
GLuint vaoTraj      = 0;
GLuint vboTraj      = 0;
int    capacidadeTraj = 0; // numero de vec3 alocados no VBO

// Cruz central (overlay) — indica visualmente quando estamos em modo
// edicao de trajetoria. Roxa quando ativo, branca em navegacao normal.
bool   modoEdicao        = false;
GLuint programaCrosshair = 0;
GLuint vaoCrosshair      = 0;
GLuint vboCrosshair      = 0;

// A camera e o "tempo" precisam ser globais por causa dos callbacks
// de GLFW (que sao funcoes livres).
Camera camera(vec3(0.0f, 1.5f, 7.0f));
float  deltaTime  = 0.0f;
float  lastFrame  = 0.0f;
float  lastX      = WIDTH  / 2.0f;
float  lastY      = HEIGHT / 2.0f;
bool   firstMouse = true;

// -------------------------------------------------------------
// Shaders — Phong com 3 luzes + textura opcional
// -------------------------------------------------------------

const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 posicao;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 fragPos;
out vec3 fragNormal;
out vec2 fragUV;

void main() {
    vec4 worldPos = model * vec4(posicao, 1.0);
    fragPos    = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(model))) * normal;
    fragUV     = texCoord;
    gl_Position = proj * view * worldPos;
}
)";

const char* fragmentShaderSrc = R"(
#version 330 core
in vec3 fragPos;
in vec3 fragNormal;
in vec2 fragUV;

out vec4 corFinal;

uniform vec3 viewPos;

// Tres luzes: 0=principal  1=preenchimento  2=fundo
uniform vec3 lightPos[3];
uniform vec3 lightColor[3];
uniform int  lightEnabled[3];

uniform vec3  Ka;
uniform vec3  Kd;
uniform vec3  Ks;
uniform float brilho;

uniform bool      usarTextura;
uniform sampler2D texDifusa;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPos);

    vec3 corDifusa = usarTextura ? texture(texDifusa, fragUV).rgb : Kd;

    // Sem ambiente global — desligar todas as luzes faz a cena ficar
    // totalmente preta. Cada luz so contribui na face que ela atinge.
    vec3 resultado = vec3(0.0);

    for (int i = 0; i < 3; i++) {
        if (lightEnabled[i] == 0) continue;

        vec3 L = normalize(lightPos[i] - fragPos);
        vec3 R = reflect(-L, N);

        float diff     = max(dot(N, L), 0.0);
        vec3  difusa   = diff * lightColor[i] * corDifusa;

        float spec     = pow(max(dot(V, R), 0.0), brilho);
        vec3  especular = Ks * spec * lightColor[i];

        resultado += difusa + especular;
    }

    corFinal = vec4(resultado, 1.0);
}
)";

// Shader minimalista (sem iluminacao) usado para desenhar a
// trajetoria do objeto selecionado — pontos de controle e linhas
// ligando-os, em uma cor uniforme.
const char* vsTrajetoria = R"(
#version 330 core
layout(location = 0) in vec3 posicao;
uniform mat4 view;
uniform mat4 proj;
void main() {
    gl_Position = proj * view * vec4(posicao, 1.0);
}
)";

const char* fsTrajetoria = R"(
#version 330 core
uniform vec3 cor;
out vec4 corFinal;
void main() { corFinal = vec4(cor, 1.0); }
)";

// Shader da cruz central (screen-space). Recebe vertices ja em NDC
// e compensa o aspect da janela para manter os bracos com o mesmo
// comprimento aparente em pixels.
const char* vsCrosshair = R"(
#version 330 core
layout(location = 0) in vec2 pos;
uniform float aspect;
void main() {
    gl_Position = vec4(pos.x / aspect, pos.y, 0.0, 1.0);
}
)";

const char* fsCrosshair = R"(
#version 330 core
uniform vec3 cor;
out vec4 corFinal;
void main() { corFinal = vec4(cor, 1.0); }
)";

// -------------------------------------------------------------
// Compilacao de shaders
// -------------------------------------------------------------

static GLuint compilaShader(const char* src, GLenum tipo) {
    GLuint s = glCreateShader(tipo);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        cerr << "Shader error: " << log << endl;
    }
    return s;
}

static GLuint linkaPrograma(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compilaShader(vsSrc, GL_VERTEX_SHADER);
    GLuint fs = compilaShader(fsSrc, GL_FRAGMENT_SHADER);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        cerr << "Link error: " << log << endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

static GLuint criaPrograma() {
    return linkaPrograma(vertexShaderSrc, fragmentShaderSrc);
}

// -------------------------------------------------------------
// Utilitarios de caminho
// -------------------------------------------------------------

static bool arquivoExiste(const string& p) {
    ifstream f(p); return f.good();
}

static string resolvePath(const string& rel) {
    for (const string& prefix : { string(""), string("./"), string("../"), string("../../") }) {
        string p = prefix + rel;
        if (arquivoExiste(p)) return p;
    }
    return rel;
}

static string diretorioDe(const string& p) {
    size_t pos = p.find_last_of("/\\");
    return (pos == string::npos) ? string("") : p.substr(0, pos + 1);
}

// -------------------------------------------------------------
// loadMTL — le Ka, Kd, Ks, Ns e map_Kd do arquivo .MTL
// -------------------------------------------------------------

MaterialMTL loadMTL(const string& filePath) {
    MaterialMTL mat;
    ifstream arq(filePath);
    if (!arq.is_open()) {
        cerr << "Aviso: nao foi possivel abrir MTL: " << filePath << endl;
        return mat;
    }

    string linha;
    while (getline(arq, linha)) {
        if (linha.empty() || linha[0] == '#') continue;
        istringstream iss(linha);
        string tag; iss >> tag;

        if (tag == "Ka")        iss >> mat.Ka.r >> mat.Ka.g >> mat.Ka.b;
        else if (tag == "Kd")   iss >> mat.Kd.r >> mat.Kd.g >> mat.Kd.b;
        else if (tag == "Ks")   iss >> mat.Ks.r >> mat.Ks.g >> mat.Ks.b;
        else if (tag == "Ns")   iss >> mat.Ns;
        else if (tag == "map_Kd") iss >> mat.mapKd;
    }

    cout << "MTL lido: " << filePath
         << " | Ka=(" << mat.Ka.r << "," << mat.Ka.g << "," << mat.Ka.b << ")"
         << " Kd=(" << mat.Kd.r << "," << mat.Kd.g << "," << mat.Kd.b << ")"
         << " Ks=(" << mat.Ks.r << "," << mat.Ks.g << "," << mat.Ks.b << ")"
         << " Ns=" << mat.Ns << endl;
    return mat;
}

GLuint loadTexture(const string& filePath, bool pixelArt = false) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Pixel-art (PSX/sprites): nearest, sem mipmap, preserva pixels nitidos.
    // Outras texturas: filtro linear com mipmap para qualidade.
    if (pixelArt) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    int w, h, canais;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &canais, 0);
    if (data) {
        GLenum formato = (canais == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, formato, w, h, 0, formato, GL_UNSIGNED_BYTE, data);
        if (!pixelArt) glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura carregada: " << filePath << " (" << w << "x" << h
             << (pixelArt ? ", nearest" : "") << ")" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID); texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// -------------------------------------------------------------
// Trajetorias — atualizacao por interpolacao linear ciclica
// -------------------------------------------------------------

// Avanca o parametro t do segmento atual proporcionalmente ao
// dt e a distancia do segmento, mantendo a velocidade constante.
// Ao chegar no fim (t>=1), passa ao proximo segmento; ao passar
// pelo ultimo, volta ao primeiro (ciclico).
static void atualizaTrajetoria(Objeto3D& obj, float dt) {
    Trajetoria& tr = obj.trajetoria;
    int n = (int)tr.pontos.size();
    if (!tr.ativa || n < 2) {
        if (n == 1) obj.posicao = tr.pontos[0];
        return;
    }

    vec3 p0 = tr.pontos[tr.segmento];
    vec3 p1 = tr.pontos[(tr.segmento + 1) % n];

    float dist = length(p1 - p0);
    if (dist < 1e-5f) {
        tr.segmento = (tr.segmento + 1) % n;
        tr.t = 0.0f;
        return;
    }

    tr.t += (tr.velocidade * dt) / dist;
    while (tr.t >= 1.0f) {
        tr.t -= 1.0f;
        tr.segmento = (tr.segmento + 1) % n;
        p0 = tr.pontos[tr.segmento];
        p1 = tr.pontos[(tr.segmento + 1) % n];
    }

    obj.posicao = mix(p0, p1, tr.t);
}

// Carrega o arquivo de trajetorias do disco e adiciona os pontos
// aos objetos correspondentes. Limpa as trajetorias previas antes.
// Formato: <nome_objeto> <x> <y> <z>   (linhas com # sao comentarios)
static void carregarTrajetorias(const string& caminho) {
    for (Objeto3D& o : objetos) {
        o.trajetoria.pontos.clear();
        o.trajetoria.segmento = 0;
        o.trajetoria.t        = 0.0f;
    }

    string p = resolvePath(caminho);
    ifstream arq(p);
    if (!arq.is_open()) {
        cerr << "Aviso: nao foi possivel abrir " << p << endl;
        return;
    }

    int total = 0;
    string linha;
    while (getline(arq, linha)) {
        if (linha.empty() || linha[0] == '#') continue;
        istringstream iss(linha);
        string nome; vec3 ponto;
        if (!(iss >> nome >> ponto.x >> ponto.y >> ponto.z)) continue;

        for (Objeto3D& o : objetos) {
            if (o.nome == nome) {
                o.trajetoria.pontos.push_back(ponto);
                total++;
                break;
            }
        }
    }
    cout << "Trajetorias carregadas: " << total << " pontos de " << p << endl;
}

// Salva todas as trajetorias dos objetos da cena no arquivo
// indicado, no mesmo formato lido por carregarTrajetorias.
static void salvarTrajetorias(const string& caminho) {
    // resolvePath devolve o caminho relativo (com o prefixo correto)
    // quando o arquivo ja existe — assim sobrescrevemos no mesmo
    // lugar de onde foi carregado.
    string p = resolvePath(caminho);
    ofstream arq(p);
    if (!arq.is_open()) {
        cerr << "Erro ao salvar trajetorias em " << p << endl;
        return;
    }

    arq << "# Trajetorias dos objetos da cena (desafioMod5)\n";
    arq << "# Formato: <nome_objeto> <x> <y> <z>\n\n";

    int total = 0;
    for (const Objeto3D& o : objetos) {
        if (o.trajetoria.pontos.empty()) continue;
        for (const vec3& p : o.trajetoria.pontos) {
            arq << o.nome << "  "
                << p.x << " " << p.y << " " << p.z << "\n";
            total++;
        }
        arq << "\n";
    }
    cout << "Trajetorias salvas: " << total << " pontos em " << p << endl;
}

// (Re)inicializa o VBO usado para desenhar a trajetoria do objeto
// selecionado. Chamado apenas quando os pontos mudam.
static void uploadTrajetoriaVBO(const Trajetoria& tr) {
    if (vaoTraj == 0) {
        glGenVertexArrays(1, &vaoTraj);
        glGenBuffers     (1, &vboTraj);
        glBindVertexArray(vaoTraj);
        glBindBuffer(GL_ARRAY_BUFFER, vboTraj);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }
    if (tr.pontos.empty()) { capacidadeTraj = 0; return; }

    glBindBuffer(GL_ARRAY_BUFFER, vboTraj);
    glBufferData(GL_ARRAY_BUFFER,
                 tr.pontos.size() * sizeof(vec3),
                 tr.pontos.data(),
                 GL_DYNAMIC_DRAW);
    capacidadeTraj = (int)tr.pontos.size();
}

// Desenha a trajetoria do objeto selecionado como LINE_LOOP
// (fecha ciclicamente) e marcadores nos pontos de controle.
static void desenhaTrajetoria(const mat4& view, const mat4& proj) {
    if (capacidadeTraj < 1 || programaTraj == 0) return;
    glUseProgram(programaTraj);
    glUniformMatrix4fv(glGetUniformLocation(programaTraj, "view"), 1, GL_FALSE, value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(programaTraj, "proj"), 1, GL_FALSE, value_ptr(proj));

    glBindVertexArray(vaoTraj);

    // Linhas amarelas conectando os pontos (ciclico).
    if (capacidadeTraj >= 2) {
        glUniform3f(glGetUniformLocation(programaTraj, "cor"), 1.0f, 0.85f, 0.2f);
        glDrawArrays(GL_LINE_LOOP, 0, capacidadeTraj);
    }
    // Marcadores dos pontos de controle em magenta.
    glPointSize(10.0f);
    glUniform3f(glGetUniformLocation(programaTraj, "cor"), 1.0f, 0.2f, 0.8f);
    glDrawArrays(GL_POINTS, 0, capacidadeTraj);

    glBindVertexArray(0);
}

// -------------------------------------------------------------
// Cruz central — overlay de 2 linhas (horizontal + vertical) em
// torno do centro da tela. Cor muda conforme o modo edicao.
// -------------------------------------------------------------

static void inicializaCrosshair() {
    programaCrosshair = linkaPrograma(vsCrosshair, fsCrosshair);

    // 4 vertices em NDC formando um "+" centralizado (eixo X sera
    // corrigido pelo aspect no vertex shader).
    const float s = 0.025f;
    const float verts[] = {
        -s,  0.0f,    s,  0.0f,  // linha horizontal
         0.0f, -s,    0.0f, s,   // linha vertical
    };

    glGenVertexArrays(1, &vaoCrosshair);
    glGenBuffers     (1, &vboCrosshair);
    glBindVertexArray(vaoCrosshair);
    glBindBuffer(GL_ARRAY_BUFFER, vboCrosshair);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

static void desenhaCrosshair(float aspect) {
    if (programaCrosshair == 0) return;

    glUseProgram(programaCrosshair);
    glUniform1f(glGetUniformLocation(programaCrosshair, "aspect"), aspect);

    // Roxo em modo edicao, branco em modo navegacao.
    const vec3 cor = modoEdicao ? vec3(0.7f, 0.2f, 1.0f)
                                : vec3(1.0f, 1.0f, 1.0f);
    glUniform3fv(glGetUniformLocation(programaCrosshair, "cor"),
                 1, value_ptr(cor));

    // Desenha por cima de toda a cena, ignorando profundidade.
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glBindVertexArray(vaoCrosshair);
    glDrawArrays(GL_LINES, 0, 4);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// -------------------------------------------------------------
// criaCuboTexturizado — cubo unitario procedural com UVs (0,0)-(1,1)
// em cada uma das 6 faces. Permite aplicar uma textura por inteiro
// em cada lado, em vez do unwrap em cruz usado pelo Cube.obj.
//
// Layout do VAO igual ao do loadSimpleOBJ:
//   pos(3) + normal(3) + uv(2) = 8 floats por vertice, 36 vertices.
// -------------------------------------------------------------

static GLuint criaCuboTexturizado(int& nVertices) {
    const float s = 1.0f;
    // 6 faces × 2 triangulos × 3 vertices, sempre na ordem
    // (px,py,pz, nx,ny,nz, u,v).
    const GLfloat verts[] = {
        // +Z (frente)
        -s,-s, s,  0,0, 1,  0,0,
         s,-s, s,  0,0, 1,  1,0,
         s, s, s,  0,0, 1,  1,1,
        -s,-s, s,  0,0, 1,  0,0,
         s, s, s,  0,0, 1,  1,1,
        -s, s, s,  0,0, 1,  0,1,
        // -Z (tras)
         s,-s,-s,  0,0,-1,  0,0,
        -s,-s,-s,  0,0,-1,  1,0,
        -s, s,-s,  0,0,-1,  1,1,
         s,-s,-s,  0,0,-1,  0,0,
        -s, s,-s,  0,0,-1,  1,1,
         s, s,-s,  0,0,-1,  0,1,
        // +X (direita)
         s,-s, s,  1,0, 0,  0,0,
         s,-s,-s,  1,0, 0,  1,0,
         s, s,-s,  1,0, 0,  1,1,
         s,-s, s,  1,0, 0,  0,0,
         s, s,-s,  1,0, 0,  1,1,
         s, s, s,  1,0, 0,  0,1,
        // -X (esquerda)
        -s,-s,-s, -1,0, 0,  0,0,
        -s,-s, s, -1,0, 0,  1,0,
        -s, s, s, -1,0, 0,  1,1,
        -s,-s,-s, -1,0, 0,  0,0,
        -s, s, s, -1,0, 0,  1,1,
        -s, s,-s, -1,0, 0,  0,1,
        // +Y (topo)
        -s, s, s,  0,1, 0,  0,0,
         s, s, s,  0,1, 0,  1,0,
         s, s,-s,  0,1, 0,  1,1,
        -s, s, s,  0,1, 0,  0,0,
         s, s,-s,  0,1, 0,  1,1,
        -s, s,-s,  0,1, 0,  0,1,
        // -Y (base)
        -s,-s,-s,  0,-1,0,  0,0,
         s,-s,-s,  0,-1,0,  1,0,
         s,-s, s,  0,-1,0,  1,1,
        -s,-s,-s,  0,-1,0,  0,0,
         s,-s, s,  0,-1,0,  1,1,
        -s,-s, s,  0,-1,0,  0,1,
    };

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 8 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = sizeof(verts) / sizeof(GLfloat) / 8;
    return VAO;
}

// Le apenas a linha "mtllib" do .OBJ para descobrir o .MTL referenciado.
static string getMTLName(const string& filePath) {
    ifstream arq(filePath);
    string linha;
    while (getline(arq, linha)) {
        istringstream iss(linha);
        string tag; iss >> tag;
        if (tag == "mtllib") { string nome; iss >> nome; return nome; }
    }
    return "";
}

int loadSimpleOBJ(const string& filePath, int& nVertices) {
    ifstream arq(filePath);
    if (!arq.is_open()) {
        cerr << "Erro ao tentar ler o arquivo " << filePath << endl;
        return -1;
    }

    vector<vec3>    vertices;
    vector<vec2>    texCoords;
    vector<vec3>    normals;
    vector<GLfloat> vBuffer;

    string linha;
    while (getline(arq, linha)) {
        if (linha.empty() || linha[0] == '#') continue;
        istringstream iss(linha);
        string tag; iss >> tag;

        if (tag == "v")       { vec3 v; iss >> v.x >> v.y >> v.z; vertices.push_back(v); }
        else if (tag == "vt") { vec2 t; iss >> t.x >> t.y;        texCoords.push_back(t); }
        else if (tag == "vn") { vec3 n; iss >> n.x >> n.y >> n.z; normals.push_back(n); }
        else if (tag == "f") {
            vector<ivec3> faceVerts;
            string token;
            while (iss >> token) {
                int vi = 0, ti = 0, ni = 0;
                size_t b1 = token.find('/');
                if (b1 == string::npos) {
                    vi = stoi(token);
                } else {
                    vi = stoi(token.substr(0, b1));
                    size_t b2 = token.find('/', b1 + 1);
                    if (b2 == string::npos) {
                        if (b1 + 1 < token.size()) ti = stoi(token.substr(b1 + 1));
                    } else {
                        if (b2 > b1 + 1)       ti = stoi(token.substr(b1 + 1, b2 - b1 - 1));
                        if (b2 + 1 < token.size()) ni = stoi(token.substr(b2 + 1));
                    }
                }
                faceVerts.push_back(ivec3(vi, ti, ni));
            }

            auto emite = [&](ivec3 idx) {
                vec3 p = (idx.x > 0) ? vertices [idx.x - 1] : vec3(0.0f);
                vec2 t = (idx.y > 0) ? texCoords[idx.y - 1] : vec2(0.0f);
                vec3 n = (idx.z > 0) ? normals  [idx.z - 1] : vec3(0.0f, 1.0f, 0.0f);
                vBuffer.insert(vBuffer.end(),
                    { p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y });
            };

            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                emite(faceVerts[0]);
                emite(faceVerts[i]);
                emite(faceVerts[i + 1]);
            }
        }
    }

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vBuffer.size() * sizeof(GLfloat),
                 vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 8 * sizeof(GLfloat);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    nVertices = (int)(vBuffer.size() / 8);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    cout << "OBJ lido: " << filePath << " | vertices=" << nVertices << endl;
    return (int)VAO;
}

// criaObjeto — carrega OBJ + MTL completo (Ka, Kd, Ks, Ns, map_Kd)
static Objeto3D criaObjeto(const string& nome, const string& objRel) {
    Objeto3D obj;
    obj.nome = nome;

    string objPath = resolvePath(objRel);
    string mtlFile = getMTLName(objPath);
    int    vao     = loadSimpleOBJ(objPath, obj.nVertices);
    if (vao < 0) return obj;

    obj.VAO = (GLuint)vao;

    if (!mtlFile.empty()) {
        string dir     = diretorioDe(objPath);
        string mtlPath = dir + mtlFile;
        MaterialMTL mat = loadMTL(mtlPath);

        obj.Ka     = mat.Ka;
        obj.Kd     = mat.Kd;
        obj.Ks     = mat.Ks;
        obj.brilho = mat.Ns;

        if (!mat.mapKd.empty()) {
            string texPath = dir + mat.mapKd;
            if (arquivoExiste(texPath)) {
                obj.texID      = loadTexture(texPath);
                obj.temTextura = (obj.texID != 0);
            } else {
                cerr << "Textura referenciada nao encontrada: " << texPath << endl;
            }
        }
    }

    cout << "Objeto criado: " << nome
         << " | textura: " << (obj.temTextura ? "sim" : "nao") << endl;
    return obj;
}

// -------------------------------------------------------------
// Callbacks GLFW — delegam para a instancia global de Camera
// -------------------------------------------------------------

static void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) { glfwSetWindowShouldClose(window, true); return; }

    if (key == GLFW_KEY_M) {
        modoWireframe = !modoWireframe;
        cout << (modoWireframe ? "Wireframe" : "Solido") << endl;
        return;
    }

    if (key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_3) {
        int idx = key - GLFW_KEY_1;
        luzes[idx].ativa = !luzes[idx].ativa;
        const char* nomes[] = { "principal", "preenchimento", "fundo" };
        cout << "Luz " << nomes[idx]
             << (luzes[idx].ativa ? ": LIGADA" : ": DESLIGADA") << endl;
        return;
    }

    // --- Trajetorias ---
    if (objetos.empty()) return;

    if (key == GLFW_KEY_TAB) {
        objetoSelecionado = (objetoSelecionado + 1) % (int)objetos.size();
        const Objeto3D& o = objetos[objetoSelecionado];
        cout << "Selecionado: [" << objetoSelecionado << "] " << o.nome
             << " (" << o.trajetoria.pontos.size() << " pontos)" << endl;
        uploadTrajetoriaVBO(o.trajetoria);
        return;
    }
    if (key == GLFW_KEY_N) {
        modoEdicao = !modoEdicao;
        cout << "Modo edicao " << (modoEdicao ? "ATIVADO (cruz roxa)"
                                              : "DESATIVADO (cruz branca)")
             << endl;
        return;
    }
    if (key == GLFW_KEY_P) {
        if (!modoEdicao) {
            cout << "Para adicionar pontos, ative o modo edicao com N" << endl;
            return;
        }
        Objeto3D& o = objetos[objetoSelecionado];
        o.trajetoria.pontos.push_back(camera.Position);
        o.trajetoria.segmento = 0;
        o.trajetoria.t        = 0.0f;
        cout << "Ponto adicionado em "
             << camera.Position.x << ", " << camera.Position.y << ", " << camera.Position.z
             << " (trajetoria de " << o.nome << " agora tem "
             << o.trajetoria.pontos.size() << " pontos)" << endl;
        uploadTrajetoriaVBO(o.trajetoria);
        return;
    }
    if (key == GLFW_KEY_C) {
        Objeto3D& o = objetos[objetoSelecionado];
        o.trajetoria.pontos.clear();
        o.trajetoria.segmento = 0;
        o.trajetoria.t        = 0.0f;
        cout << "Trajetoria de " << o.nome << " limpa" << endl;
        uploadTrajetoriaVBO(o.trajetoria);
        return;
    }
    if (key == GLFW_KEY_T) {
        Objeto3D& o = objetos[objetoSelecionado];
        o.trajetoria.ativa = !o.trajetoria.ativa;
        cout << "Trajetoria de " << o.nome
             << (o.trajetoria.ativa ? ": REPRODUZINDO" : ": PAUSADA") << endl;
        return;
    }
    if (key == GLFW_KEY_L) {
        carregarTrajetorias(ARQUIVO_TRAJETORIAS);
        uploadTrajetoriaVBO(objetos[objetoSelecionado].trajetoria);
        return;
    }
    if (key == GLFW_KEY_O) {
        salvarTrajetorias(ARQUIVO_TRAJETORIAS);
        return;
    }
}

static void mouse_callback(GLFWwindow*, double xposd, double yposd) {
    float xpos = (float)xposd;
    float ypos = (float)yposd;
    if (firstMouse) {
        lastX = xpos; lastY = ypos;
        firstMouse = false;
    }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // invertido: y na tela cresce para baixo
    lastX = xpos; lastY = ypos;
    camera.rotacionar(xoffset, yoffset);
}

static void scroll_callback(GLFWwindow*, double, double yoffset) {
    camera.zoom((float)yoffset);
}

static void framebuffer_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void processarInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.mover(CameraMovement::FORWARD,  deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.mover(CameraMovement::BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.mover(CameraMovement::LEFT,     deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.mover(CameraMovement::RIGHT,    deltaTime);
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.mover(CameraMovement::UP,       deltaTime);
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.mover(CameraMovement::DOWN,     deltaTime);
}

// -------------------------------------------------------------
// main
// -------------------------------------------------------------

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* janela = glfwCreateWindow(WIDTH, HEIGHT,
        "Desafio Modulo 6 - Trajetorias Parametricas", nullptr, nullptr);
    if (!janela) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(janela);
    glfwSetKeyCallback(janela, key_callback);
    glfwSetCursorPosCallback(janela, mouse_callback);
    glfwSetScrollCallback(janela, scroll_callback);
    glfwSetFramebufferSizeCallback(janela, framebuffer_callback);
    glfwSetInputMode(janela, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Erro ao inicializar GLAD" << endl;
        return -1;
    }

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;
    cout << "\n=========== CONTROLES ===========" << endl;
    cout << "WASD       : mover | Q/E : descer/subir" << endl;
    cout << "Mouse      : olhar ao redor   | Scroll: zoom" << endl;
    cout << "1/2/3      : luz principal / preenchimento / fundo on-off" << endl;
    cout << "M          : wireframe | ESC: sair" << endl;
    cout << "-- Trajetorias --" << endl;
    cout << "TAB        : selecionar proximo objeto" << endl;
    cout << "N          : modo edicao on/off (cruz central fica roxa)" << endl;
    cout << "P          : adicionar ponto (so com modo edicao ATIVO)" << endl;
    cout << "C          : limpar trajetoria do selecionado" << endl;
    cout << "T          : pausar/retomar animacao do selecionado" << endl;
    cout << "L / O      : recarregar / salvar assets/trajetorias.txt" << endl;
    cout << "=================================" << endl;

    GLuint programa = criaPrograma();
    programaTraj    = linkaPrograma(vsTrajetoria, fsTrajetoria);
    inicializaCrosshair();
    glEnable(GL_DEPTH_TEST);

    // Cena: Suzanne a esquerda, Cubo a direita, Suzanne extra ao fundo.
    // Material (Ka/Kd/Ks/Ns) lido do .MTL de cada objeto.
    Objeto3D suzanne = criaObjeto("Suzanne", "assets/Modelos3D/Suzanne.obj");
    suzanne.posicao  = vec3(-2.0f, 0.0f, 0.0f);
    if (suzanne.VAO) objetos.push_back(suzanne);

    // Cubo procedural com UVs (0,0)-(1,1) em cada face, texturizado com
    // o primeiro caixote do sprite sheet de Crash Bandicoot. Usa filtro
    // GL_NEAREST para preservar a aparencia pixel-art original.
    Objeto3D cubo;
    cubo.nome    = "CrateCrash";
    cubo.VAO     = criaCuboTexturizado(cubo.nVertices);
    cubo.posicao = vec3(2.0f, 0.0f, 0.0f);
    cubo.Ka      = vec3(1.0f); // textura como cor — sem tingimento
    cubo.Kd      = vec3(1.0f);
    cubo.Ks      = vec3(0.3f);
    cubo.brilho  = 16.0f;
    string cratePath = resolvePath("assets/tex/CrashCrate.png");
    cubo.texID      = loadTexture(cratePath, /*pixelArt=*/true);
    cubo.temTextura = (cubo.texID != 0);
    objetos.push_back(cubo);

    Objeto3D suzanneFundo = criaObjeto("SuzanneFundo", "assets/Modelos3D/Suzanne.obj");
    suzanneFundo.posicao  = vec3(0.0f, 1.0f, -5.0f);
    if (suzanneFundo.VAO) objetos.push_back(suzanneFundo);

    if (objetos.empty()) {
        cerr << "Nenhum objeto carregado. Verifique assets/Modelos3D/." << endl;
        glfwTerminate();
        return -1;
    }

    // Carrega as trajetorias do arquivo de configuracao e seleciona o
    // CrateCrash como objeto editavel por padrao.
    carregarTrajetorias(ARQUIVO_TRAJETORIAS);
    for (int i = 0; i < (int)objetos.size(); i++) {
        if (objetos[i].nome == "CrateCrash") { objetoSelecionado = i; break; }
    }
    uploadTrajetoriaVBO(objetos[objetoSelecionado].trajetoria);
    cout << "Selecionado: [" << objetoSelecionado << "] "
         << objetos[objetoSelecionado].nome << endl;

    glUseProgram(programa);
    const GLint locModel   = glGetUniformLocation(programa, "model");
    const GLint locView    = glGetUniformLocation(programa, "view");
    const GLint locProj    = glGetUniformLocation(programa, "proj");
    const GLint locViewPos = glGetUniformLocation(programa, "viewPos");
    const GLint locKa      = glGetUniformLocation(programa, "Ka");
    const GLint locKd      = glGetUniformLocation(programa, "Kd");
    const GLint locKs      = glGetUniformLocation(programa, "Ks");
    const GLint locBrilho  = glGetUniformLocation(programa, "brilho");
    const GLint locTex     = glGetUniformLocation(programa, "texDifusa");
    const GLint locUsarTex = glGetUniformLocation(programa, "usarTextura");
    glUniform1i(locTex, 0);

    GLint locLightPos[3], locLightColor[3], locLightEnabled[3];
    for (int i = 0; i < 3; i++) {
        string lp = "lightPos["     + to_string(i) + "]";
        string lc = "lightColor["   + to_string(i) + "]";
        string le = "lightEnabled[" + to_string(i) + "]";
        locLightPos    [i] = glGetUniformLocation(programa, lp.c_str());
        locLightColor  [i] = glGetUniformLocation(programa, lc.c_str());
        locLightEnabled[i] = glGetUniformLocation(programa, le.c_str());
    }

    while (!glfwWindowShouldClose(janela)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();
        processarInput(janela);

        // Atualiza a posicao de cada objeto conforme sua trajetoria
        // (interpolacao linear ciclica entre pontos de controle).
        for (Objeto3D& obj : objetos) atualizaTrajetoria(obj, deltaTime);

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, modoWireframe ? GL_LINE : GL_FILL);

        glUseProgram(programa);

        int fw, fh;
        glfwGetFramebufferSize(janela, &fw, &fh);
        float aspect = (fw > 0 && fh > 0) ? (float)fw / fh : 1.0f;

        mat4 view = camera.getViewMatrix();
        mat4 proj = camera.getProjectionMatrix(aspect);

        glUniformMatrix4fv(locView, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(locProj, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(locViewPos, 1, value_ptr(camera.Position));

        for (int i = 0; i < 3; i++) {
            glUniform3fv(locLightPos    [i], 1, value_ptr(luzes[i].pos));
            glUniform3fv(locLightColor  [i], 1, value_ptr(luzes[i].color));
            glUniform1i (locLightEnabled[i], luzes[i].ativa ? 1 : 0);
        }

        for (Objeto3D& obj : objetos) {
            mat4 model(1.0f);
            model = translate(model, obj.posicao);
            model = rotate(model, radians(obj.rotacao.x), vec3(1, 0, 0));
            model = rotate(model, radians(obj.rotacao.y), vec3(0, 1, 0));
            model = rotate(model, radians(obj.rotacao.z), vec3(0, 0, 1));
            model = scale(model, obj.escala);
            glUniformMatrix4fv(locModel, 1, GL_FALSE, value_ptr(model));

            glUniform3fv(locKa,     1, value_ptr(obj.Ka));
            glUniform3fv(locKd,     1, value_ptr(obj.Kd));
            glUniform3fv(locKs,     1, value_ptr(obj.Ks));
            glUniform1f (locBrilho, obj.brilho);

            if (obj.temTextura) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, obj.texID);
                glUniform1i(locUsarTex, GL_TRUE);
            } else {
                glUniform1i(locUsarTex, GL_FALSE);
            }

            glBindVertexArray(obj.VAO);
            glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
            glBindVertexArray(0);
        }

        // Desenha a trajetoria do objeto selecionado por cima da cena
        // (linhas amarelas conectam os pontos de controle, pontos em
        // magenta marcam cada vertice).
        desenhaTrajetoria(view, proj);

        // Cruz central como overlay (HUD). Cor reflete o modo edicao.
        desenhaCrosshair(aspect);

        glfwSwapBuffers(janela);
    }

    for (Objeto3D& obj : objetos) {
        if (obj.VAO)   glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID) glDeleteTextures(1, &obj.texID);
    }
    if (vboTraj)           glDeleteBuffers(1, &vboTraj);
    if (vaoTraj)           glDeleteVertexArrays(1, &vaoTraj);
    if (programaTraj)      glDeleteProgram(programaTraj);
    if (vboCrosshair)      glDeleteBuffers(1, &vboCrosshair);
    if (vaoCrosshair)      glDeleteVertexArrays(1, &vaoCrosshair);
    if (programaCrosshair) glDeleteProgram(programaCrosshair);
    glDeleteProgram(programa);
    glfwTerminate();
    return 0;
}

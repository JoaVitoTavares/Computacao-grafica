// =============================================================
// Projeto Final — Cenario com base de blocos de grama e arvores
//
// Baseado no Desafio do Modulo 6 (camera sintetica em 1a pessoa
// com projecao perspectiva e iluminacao Phong multiluz). Aqui o
// foco passa a ser a montagem de um pequeno CENARIO:
//
//   - Uma BASE/chao formada por varios cubos posicionados lado a
//     lado, formando uma grade (grid) de blocos.
//   - Cada bloco e texturizado com uma textura de FOLHAGEM (grama)
//     gerada proceduralmente em assets/tex/grass.png.
//   - Acima dos blocos sao posicionadas ARVORES, desenhadas com a
//     tecnica de billboard cruzado (duas quads perpendiculares)
//     usando uma textura de arvore com transparencia
//     (assets/tex/tree.png).
//
// A camera continua sendo a classe do M5/M6 (Mover WASD/QE e
// Rotacionar pelo mouse) e a iluminacao Phong do M4.
//
// Manuseio de objetos / trajetorias parametricas (M6): caixa, arvores
// e a lua sao objetos "editaveis" — cada um pode ter uma trajetoria
// ciclica de pontos de controle. A interpolacao e LINEAR por padrao
// (lua e arvores); a CAIXA do Crash usa uma curva de BEZIER cubica
// (spline fechada e suave) na sua movimentacao. A lua ja vem com uma
// orbita VERTICAL padrao, contornando o retangulo de terra.
//
// Controles:
//   W A S D        — andar (frente/tras/esquerda/direita)
//   Q / E          — descer / subir
//   Mouse          — olhar ao redor (yaw / pitch)
//   Scroll         — zoom (altera FOV)
//   1 / 2 / 3      — liga/desliga luz principal / preenchimento / fundo
//   M              — alterna solido / wireframe
//   TAB            — seleciona o proximo objeto editavel (arvores/lua)
//   N              — modo edicao on/off (cruz central fica roxa)
//   P              — adiciona ponto na posicao da camera (modo edicao)
//   C              — limpa a trajetoria do objeto selecionado
//   T              — pausa / retoma a animacao do selecionado
//   L / O          — recarrega / salva assets/trajetorias.txt
//   ESC            — fechar
// =============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <utility>

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

// Dimensoes do cenario (grade de blocos de grama).
const int   GRID_X   = 16;     // numero de blocos no eixo X
const int   GRID_Z   = 16;     // numero de blocos no eixo Z
const float BLOCO     = 1.0f;  // aresta de cada bloco (em unidades)

// -------------------------------------------------------------
// Classe Camera — encapsula posicao, orientacao e operacoes de
// movimentacao e rotacao em primeira pessoa (M5/M6).
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

    Camera(vec3 position   = vec3(0.0f, 2.0f, 8.0f),
           vec3 worldUp    = vec3(0.0f, 1.0f, 0.0f),
           float yaw       = -90.0f,
           float pitch     = -10.0f)
        : Position(position),
          Front(vec3(0.0f, 0.0f, -1.0f)),
          WorldUp(worldUp),
          Yaw(yaw),
          Pitch(pitch),
          MovementSpeed(10.0f),
          MouseSensitivity(0.1f),
          Fov(45.0f)
    {
        updateCameraVectors();
    }

    mat4 getViewMatrix() const {
        return lookAt(Position, Position + Front, Up);
    }

    mat4 getProjectionMatrix(float aspect) const {
        return perspective(radians(Fov), aspect, 0.1f, 200.0f);
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
// Estruturas — Trajetoria, Objeto3D, Luz
// -------------------------------------------------------------

// Trajetoria — lista de pontos de controle percorrida ciclicamente
// (segmento por segmento), conforme o M6 (Curvas Parametricas).
// Por padrao a interpolacao e LINEAR (grau 1). Com bezier = true,
// a curva passa a ser uma spline CUBICA DE BEZIER fechada, suave,
// passando pelos pontos de controle (caixa do Crash).
struct Trajetoria {
    vector<vec3> pontos;            // pontos de controle (em ordem)
    int          segmento   = 0;    // indice do segmento atual
    float        t          = 0.0f; // parametro [0,1] no segmento
    float        velocidade = 1.5f; // unidades por segundo
    bool         ativa      = true;
    bool         bezier     = false;// false = linear, true = Bezier cubica
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
    vec3   Ks         = vec3(0.3f);
    float  brilho     = 16.0f;
    GLuint texID      = 0;
    bool   temTextura = false;
    bool   editavel   = false;  // true = entra no ciclo de selecao (TAB)
    vec3   spin       = vec3(0.0f); // rotacao automatica (graus/seg) por eixo
    Trajetoria trajetoria;
};

struct Luz {
    vec3 pos;
    vec3 color;
    bool ativa = true;
};

// Tres luzes ao redor do cenario. A "principal" (key light) e a luz
// dominante da cena.
Luz luzes[3] = {
    { vec3(  8.0f, 14.0f,  10.0f), vec3(1.0f, 0.97f, 0.85f), true }, // luz principal (alto, frente)
    { vec3(-12.0f,  6.0f,   4.0f), vec3(0.4f, 0.55f, 0.9f),  true }, // preenchimento (ceu, lateral)
    { vec3(  0.0f,  5.0f, -16.0f), vec3(0.9f, 0.7f,  0.5f),  true }, // fundo (atras)
};

vector<Objeto3D> objetos;
bool             modoWireframe     = false;
int              objetoSelecionado = 0;     // indice do objeto editavel atual
bool             modoEdicao        = false; // adicionar pontos com P (cruz roxa)

// Caminho do arquivo de configuracao de trajetorias.
const string ARQUIVO_TRAJETORIAS = "assets/trajetorias.txt";

// Recursos do shader de visualizacao da trajetoria (pontos + linhas).
GLuint programaTraj   = 0;
GLuint vaoTraj        = 0;
GLuint vboTraj        = 0;
int    capacidadeTraj = 0;     // numero de vec3 atualmente no VBO
bool   trajCurva      = false; // VBO atual e uma curva amostrada (Bezier)?

// A camera e o "tempo" precisam ser globais por causa dos callbacks
// de GLFW (que sao funcoes livres).
Camera camera;
float  deltaTime  = 0.0f;
float  lastFrame  = 0.0f;
float  lastX      = WIDTH  / 2.0f;
float  lastY      = HEIGHT / 2.0f;
bool   firstMouse = true;

// -------------------------------------------------------------
// Shaders — Phong com 3 luzes + textura opcional. Suporta recorte
// por alpha (arvores) e iluminacao de dois lados (billboards).
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
    vec4 amostra = usarTextura ? texture(texDifusa, fragUV) : vec4(Kd, 1.0);

    // Recorte por alpha — folhas/transparencia da arvore.
    if (usarTextura && amostra.a < 0.5) discard;

    vec3 corDifusa = amostra.rgb;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPos);

    // Iluminacao de dois lados: as arvores sao billboards (quads
    // planas) e podem ser vistas por tras. Orientamos a normal para
    // o observador para que ambos os lados recebam iluminacao Phong.
    if (dot(N, V) < 0.0) N = -N;

    // Sem ambiente global (igual ao desafio do Modulo 6): desligar
    // todas as luzes deixa a cena totalmente preta. Cada luz so
    // contribui na face que ela atinge.
    vec3 resultado = vec3(0.0);

    for (int i = 0; i < 3; i++) {
        if (lightEnabled[i] == 0) continue;

        // Vetor e distancia ate a fonte de luz.
        vec3  Ldir = lightPos[i] - fragPos;
        float dist = length(Ldir);
        vec3  L    = Ldir / dist;          // direcao normalizada
        vec3  R    = reflect(-L, N);

        // Atenuacao por distancia (point light): a intensidade cai
        // conforme o ponto se afasta da fonte. Assim cada luz tem um
        // ALCANCE — perto ilumina com forca maxima, longe quase nao
        // pega. Como vale para as 3 luzes e usa fragPos/posicao do
        // objeto, o local e a intensidade da luz no objeto mudam
        // conforme ele se move pela cena.
        float atten = 1.0 / (1.0 + 0.014 * dist + 0.0003 * dist * dist);

        float diff      = max(dot(N, L), 0.0);
        vec3  difusa    = diff * lightColor[i] * corDifusa;

        float spec      = pow(max(dot(V, R), 0.0), brilho);
        vec3  especular = Ks * spec * lightColor[i];

        resultado += (difusa + especular) * atten;
    }

    corFinal = vec4(resultado, 1.0);
}
)";

// Shader da cruz central (screen-space, mira da camera).
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

// Shader minimalista (sem iluminacao) para desenhar a trajetoria do
// objeto selecionado — pontos de controle e linhas, em cor uniforme.
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

GLuint vaoCrosshair      = 0;
GLuint vboCrosshair      = 0;
GLuint programaCrosshair = 0;

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

// -------------------------------------------------------------
// Utilitarios de caminho e textura
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

GLuint loadTexture(const string& filePath, bool pixelArt = true) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
             << ", " << canais << " canais)" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID); texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// -------------------------------------------------------------
// criaCuboTexturizado — cubo de aresta 1 (de -0.5 a 0.5) com UVs
// (0,0)-(1,1) em cada uma das 6 faces, para aplicar a textura de
// grama por inteiro em cada lado.
//
// Layout do VAO: pos(3) + normal(3) + uv(2) = 8 floats/vertice.
// -------------------------------------------------------------

static GLuint criaCuboTexturizado(int& nVertices) {
    const float s = 0.5f;
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

// -------------------------------------------------------------
// criaArvoreBillboard — "billboard cruzado": duas quads
// perpendiculares (uma no plano XY, outra no plano ZY) que se
// cruzam no centro. Texturizadas com a arvore (alpha recortado),
// dao a ilusao de volume vista de qualquer angulo. A base fica em
// y=0 para apoiar a arvore sobre o topo dos blocos.
//
// largura = aresta da quad em X/Z, altura = aresta em Y.
// Layout do VAO igual ao do cubo (pos+normal+uv).
// -------------------------------------------------------------

static GLuint criaArvoreBillboard(float largura, float altura, int& nVertices) {
    const float w = largura * 0.5f;
    const float h = altura;
    // A iluminacao da arvore e de dois lados (a normal e orientada
    // para o observador no shader), entao basta uma normal coerente.
    const GLfloat verts[] = {
        // Quad A — plano XY (normal +Z)
        -w, 0.0f, 0.0f,  0,0,1,  0,0,
         w, 0.0f, 0.0f,  0,0,1,  1,0,
         w,   h,  0.0f,  0,0,1,  1,1,
        -w, 0.0f, 0.0f,  0,0,1,  0,0,
         w,   h,  0.0f,  0,0,1,  1,1,
        -w,   h,  0.0f,  0,0,1,  0,1,
        // Quad B — plano ZY (normal +X)
        0.0f, 0.0f, -w,  1,0,0,  0,0,
        0.0f, 0.0f,  w,  1,0,0,  1,0,
        0.0f,   h,   w,  1,0,0,  1,1,
        0.0f, 0.0f, -w,  1,0,0,  0,0,
        0.0f,   h,   w,  1,0,0,  1,1,
        0.0f,   h,  -w,  1,0,0,  0,1,
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

// -------------------------------------------------------------
// criaEsfera — esfera UV (latitude/longitude) de raio 1, gerada
// proceduralmente. Usada para a lua no ceu. Cada vertice carrega
// posicao, normal (= posicao normalizada) e coordenada de textura
// equiretangular (u em longitude, v em latitude).
//
// Layout do VAO igual aos demais: pos(3) + normal(3) + uv(2).
// -------------------------------------------------------------

static GLuint criaEsfera(int setores, int pilhas, int& nVertices) {
    vector<GLfloat> buf;

    auto vertice = [&](int i, int j) {
        float v   = (float)i / pilhas;           // 0..1 (polo a polo)
        float u   = (float)j / setores;          // 0..1 (volta completa)
        float phi = v * (float)M_PI;             // 0..PI
        float th  = u * 2.0f * (float)M_PI;       // 0..2PI

        float x = sinf(phi) * cosf(th);
        float y = cosf(phi);
        float z = sinf(phi) * sinf(th);

        // pos, normal (mesma direcao do ponto na esfera unitaria), uv
        buf.insert(buf.end(), { x, y, z,  x, y, z,  u, 1.0f - v });
    };

    // Dois triangulos por quad (exceto degenerados nos polos).
    for (int i = 0; i < pilhas; i++) {
        for (int j = 0; j < setores; j++) {
            vertice(i,     j);
            vertice(i + 1, j);
            vertice(i + 1, j + 1);

            vertice(i,     j);
            vertice(i + 1, j + 1);
            vertice(i,     j + 1);
        }
    }

    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(GLfloat),
                 buf.data(), GL_STATIC_DRAW);

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

    nVertices = (int)(buf.size() / 8);
    return VAO;
}

// -------------------------------------------------------------
// loadSimpleOBJ — carrega um .OBJ (v / vt / vn / f) para um VAO no
// mesmo layout dos demais (pos + normal + uv = 8 floats/vertice).
// Usado para o modelo do macaco (Suzanne).
// -------------------------------------------------------------

static int loadSimpleOBJ(const string& filePath, int& nVertices) {
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
                        if (b2 > b1 + 1)           ti = stoi(token.substr(b1 + 1, b2 - b1 - 1));
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

            // Triangula a face (fan) caso tenha mais de 3 vertices.
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

// -------------------------------------------------------------
// Trajetorias — manuseio de objetos (M6). Interpolacao linear
// ciclica entre pontos de controle, edicao em tempo de execucao
// e persistencia em assets/trajetorias.txt.
// -------------------------------------------------------------

// Avalia um segmento cubico de Bezier B(t) com 4 pontos de controle:
//   B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3
static vec3 bezierCubica(const vec3& P0, const vec3& P1,
                         const vec3& P2, const vec3& P3, float t) {
    float u  = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    return b0 * P0 + b1 * P1 + b2 * P2 + b3 * P3;
}

// Para uma spline de Bezier FECHADA e suave (C1) passando por todos
// os pontos de controle, derivamos os dois pontos-guia (handles) de
// cada segmento a partir dos vizinhos (conversao Catmull-Rom -> Bezier).
// O segmento i vai do ponto i ao i+1.
static void handlesBezier(const vector<vec3>& P, int i, vec3& h1, vec3& h2) {
    int n = (int)P.size();
    vec3 a0 = P[(i - 1 + n) % n];
    vec3 a1 = P[i];
    vec3 a2 = P[(i + 1) % n];
    vec3 a3 = P[(i + 2) % n];
    h1 = a1 + (a2 - a0) / 6.0f;
    h2 = a2 - (a3 - a1) / 6.0f;
}

// Avanca o parametro t do segmento atual proporcionalmente ao dt e
// ao comprimento do segmento (velocidade ~constante). Ao passar do
// ultimo ponto, volta ao primeiro (ciclico). A posicao do objeto e
// obtida por interpolacao LINEAR ou por uma curva de BEZIER cubica,
// conforme tr.bezier.
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

    if (tr.bezier && n >= 3) {
        vec3 h1, h2;
        handlesBezier(tr.pontos, tr.segmento, h1, h2);
        obj.posicao = bezierCubica(p0, h1, h2, p1, tr.t);
    } else {
        obj.posicao = mix(p0, p1, tr.t);
    }
}

// (Re)envia os pontos da trajetoria do objeto selecionado ao VBO de
// visualizacao. Chamado quando os pontos ou a selecao mudam.
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

    // Linear: o VBO recebe os proprios pontos de controle. Bezier: o
    // VBO recebe a curva AMOSTRADA (varios pontos por segmento), para
    // a linha desenhada acompanhar a curva suave, e nao o poligono.
    vector<vec3> dados;
    trajCurva = (tr.bezier && (int)tr.pontos.size() >= 3);
    if (trajCurva) {
        const int n = (int)tr.pontos.size();
        const int SUB = 20;                 // amostras por segmento
        for (int i = 0; i < n; i++) {
            vec3 p0 = tr.pontos[i];
            vec3 p3 = tr.pontos[(i + 1) % n];
            vec3 h1, h2;
            handlesBezier(tr.pontos, i, h1, h2);
            for (int s = 0; s < SUB; s++)
                dados.push_back(bezierCubica(p0, h1, h2, p3, (float)s / SUB));
        }
    } else {
        dados = tr.pontos;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vboTraj);
    glBufferData(GL_ARRAY_BUFFER,
                 dados.size() * sizeof(vec3),
                 dados.data(),
                 GL_DYNAMIC_DRAW);
    capacidadeTraj = (int)dados.size();
}

// Desenha a trajetoria do objeto selecionado: LINE_LOOP (ciclico)
// em amarelo + marcadores magenta nos pontos de controle.
static void desenhaTrajetoria(const mat4& view, const mat4& proj) {
    if (capacidadeTraj < 1 || programaTraj == 0) return;
    glUseProgram(programaTraj);
    glUniformMatrix4fv(glGetUniformLocation(programaTraj, "view"), 1, GL_FALSE, value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(programaTraj, "proj"), 1, GL_FALSE, value_ptr(proj));

    glBindVertexArray(vaoTraj);
    if (capacidadeTraj >= 2) {
        glUniform3f(glGetUniformLocation(programaTraj, "cor"), 1.0f, 0.85f, 0.2f);
        glDrawArrays(GL_LINE_LOOP, 0, capacidadeTraj);
    }
    // Marcadores magenta nos pontos de controle (apenas no modo linear;
    // na curva de Bezier o VBO contem amostras, nao pontos de controle).
    if (!trajCurva) {
        glPointSize(10.0f);
        glUniform3f(glGetUniformLocation(programaTraj, "cor"), 1.0f, 0.2f, 0.8f);
        glDrawArrays(GL_POINTS, 0, capacidadeTraj);
    }
    glBindVertexArray(0);
}

// Seleciona o proximo objeto marcado como editavel (ciclico). Pula
// os 256 blocos de grama, parando apenas em arvores e na lua.
static void selecionarProximoEditavel() {
    int n = (int)objetos.size();
    for (int passo = 1; passo <= n; passo++) {
        int idx = (objetoSelecionado + passo) % n;
        if (objetos[idx].editavel) {
            objetoSelecionado = idx;
            const Objeto3D& o = objetos[idx];
            cout << "Selecionado: [" << idx << "] " << o.nome
                 << " (" << o.trajetoria.pontos.size() << " pontos)" << endl;
            uploadTrajetoriaVBO(o.trajetoria);
            return;
        }
    }
}

// Carrega o arquivo de trajetorias e adiciona os pontos aos objetos
// correspondentes (por nome). Limpa as trajetorias previas antes.
// Formato: <nome_objeto> <x> <y> <z>  (# = comentario)
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

// Salva as trajetorias de todos os objetos editaveis no arquivo.
static void salvarTrajetorias(const string& caminho) {
    string p = resolvePath(caminho);
    ofstream arq(p);
    if (!arq.is_open()) {
        cerr << "Erro ao salvar trajetorias em " << p << endl;
        return;
    }

    arq << "# Trajetorias dos objetos da cena (projetofinal)\n";
    arq << "# Formato: <nome_objeto> <x> <y> <z>\n\n";

    int total = 0;
    for (const Objeto3D& o : objetos) {
        if (o.trajetoria.pontos.empty()) continue;
        for (const vec3& pt : o.trajetoria.pontos) {
            arq << o.nome << "  " << pt.x << " " << pt.y << " " << pt.z << "\n";
            total++;
        }
        arq << "\n";
    }
    cout << "Trajetorias salvas: " << total << " pontos em " << p << endl;
}

// -------------------------------------------------------------
// Cruz central (mira) — overlay de 2 linhas no centro da tela.
// -------------------------------------------------------------

static void inicializaCrosshair() {
    programaCrosshair = linkaPrograma(vsCrosshair, fsCrosshair);
    const float s = 0.02f;
    const float verts[] = {
        -s,  0.0f,    s,  0.0f,
         0.0f, -s,    0.0f, s,
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
    // Roxo em modo edicao, branco em navegacao normal.
    const vec3 cor = modoEdicao ? vec3(0.7f, 0.2f, 1.0f) : vec3(1.0f);
    glUniform3fv(glGetUniformLocation(programaCrosshair, "cor"), 1, value_ptr(cor));
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glBindVertexArray(vaoCrosshair);
    glDrawArrays(GL_LINES, 0, 4);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// -------------------------------------------------------------
// Montagem do cenario
// -------------------------------------------------------------

// Cria a base do cenario: uma grade GRID_X x GRID_Z de blocos de
// grama, todos compartilhando o mesmo VAO de cubo e a mesma textura
// de folhagem. Os blocos sao centrados na origem no plano XZ, com o
// TOPO em y = 0 (para as arvores apoiarem em cima).
static void montarChao(GLuint vaoCubo, int nVertCubo, GLuint texGrama) {
    const float offX = (GRID_X - 1) * BLOCO * 0.5f;
    const float offZ = (GRID_Z - 1) * BLOCO * 0.5f;

    for (int i = 0; i < GRID_X; i++) {
        for (int j = 0; j < GRID_Z; j++) {
            Objeto3D bloco;
            bloco.nome       = "grama_" + to_string(i) + "_" + to_string(j);
            bloco.VAO        = vaoCubo;
            bloco.nVertices  = nVertCubo;
            bloco.escala     = vec3(BLOCO);
            // centro do bloco em y = -BLOCO/2  =>  topo em y = 0
            bloco.posicao    = vec3(i * BLOCO - offX, -BLOCO * 0.5f,
                                    j * BLOCO - offZ);
            bloco.Ka         = vec3(1.0f);
            bloco.Kd         = vec3(1.0f);
            bloco.Ks         = vec3(0.05f);
            bloco.brilho     = 8.0f;
            bloco.texID      = texGrama;
            bloco.temTextura = (texGrama != 0);
            objetos.push_back(bloco);
        }
    }
}

// Posiciona uma arvore (billboard cruzado) com a base sobre o topo
// dos blocos (y = 0), na coordenada (x, z) informada.
static void plantarArvore(GLuint vaoArv, int nVertArv, GLuint texArv,
                          float x, float z, float escala = 1.0f) {
    static int contador = 0;          // nomes unicos: arvore_0, arvore_1, ...
    Objeto3D arvore;
    arvore.nome       = "arvore_" + to_string(contador++);
    arvore.VAO        = vaoArv;
    arvore.nVertices  = nVertArv;
    arvore.posicao    = vec3(x, 0.0f, z);
    arvore.escala     = vec3(escala);
    arvore.texID      = texArv;
    arvore.temTextura = (texArv != 0);
    arvore.editavel   = true;         // pode receber trajetoria (TAB/P)
    objetos.push_back(arvore);
}

// -------------------------------------------------------------
// Callbacks GLFW
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

    // --- Manuseio de objetos / trajetorias (M6) ---
    if (objetos.empty()) return;

    if (key == GLFW_KEY_TAB) {
        selecionarProximoEditavel();
        return;
    }
    if (key == GLFW_KEY_N) {
        modoEdicao = !modoEdicao;
        cout << "Modo edicao " << (modoEdicao ? "ATIVADO (cruz roxa)"
                                              : "DESATIVADO (cruz branca)") << endl;
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
        cout << "Ponto adicionado em " << camera.Position.x << ", "
             << camera.Position.y << ", " << camera.Position.z
             << " (" << o.nome << " agora tem "
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
        "Projeto Final - Cenario com blocos de grama e arvores", nullptr, nullptr);
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
    cout << "-- Manuseio de objetos (trajetorias) --" << endl;
    cout << "TAB        : selecionar proximo objeto (caixa / arvores / lua)" << endl;
    cout << "N          : modo edicao on/off (cruz central fica roxa)" << endl;
    cout << "P          : adicionar ponto na posicao da camera (modo edicao)" << endl;
    cout << "C          : limpar trajetoria do selecionado" << endl;
    cout << "T          : pausar/retomar animacao do selecionado" << endl;
    cout << "L / O      : recarregar / salvar assets/trajetorias.txt" << endl;
    cout << "=================================" << endl;

    GLuint programa = linkaPrograma(vertexShaderSrc, fragmentShaderSrc);
    programaTraj    = linkaPrograma(vsTrajetoria, fsTrajetoria);
    inicializaCrosshair();
    glEnable(GL_DEPTH_TEST);

    // ---- Recursos compartilhados do cenario ----
    int    nVertCubo = 0, nVertArv = 0, nVertLua = 0, nVertMacaco = 0;
    GLuint vaoCubo   = criaCuboTexturizado(nVertCubo);
    GLuint vaoArv    = criaArvoreBillboard(/*largura=*/2.4f, /*altura=*/3.6f, nVertArv);
    GLuint vaoLua    = criaEsfera(/*setores=*/48, /*pilhas=*/24, nVertLua);
    GLuint vaoMacaco = (GLuint)loadSimpleOBJ(resolvePath("assets/Modelos3D/Suzanne.obj"), nVertMacaco);

    GLuint texGrama  = loadTexture(resolvePath("assets/tex/grass.png"),       /*pixelArt=*/true);
    GLuint texArvore = loadTexture(resolvePath("assets/tex/tree.png"),        /*pixelArt=*/true);
    GLuint texLua    = loadTexture(resolvePath("assets/tex/moon.png"),        /*pixelArt=*/false);
    GLuint texCaixa  = loadTexture(resolvePath("assets/tex/CrashCrate.png"),  /*pixelArt=*/true);
    GLuint texMacaco = loadTexture(resolvePath("assets/Modelos3D/Suzanne.png"),/*pixelArt=*/false);

    // ---- Base de blocos de grama ----
    montarChao(vaoCubo, nVertCubo, texGrama);

    // ---- Arvores sobre os blocos ----
    plantarArvore(vaoArv, nVertArv, texArvore, -4.0f, -3.0f, 1.0f);
    plantarArvore(vaoArv, nVertArv, texArvore,  3.0f, -1.0f, 1.2f);
    plantarArvore(vaoArv, nVertArv, texArvore, -1.0f,  3.0f, 0.9f);
    plantarArvore(vaoArv, nVertArv, texArvore,  5.0f,  4.0f, 1.1f);
    plantarArvore(vaoArv, nVertArv, texArvore,  0.0f, -5.0f, 1.0f);
    plantarArvore(vaoArv, nVertArv, texArvore, -5.0f,  1.0f, 0.85f);

    // ---- Lua grande e distante no ceu ----
    // Iluminada pelo mesmo Phong da cena: depende das 3 luzes. Com
    // todas desligadas (1/2/3) a lua fica preta, e o lado iluminado
    // muda conforme ela percorre a orbita (interage de forma diferente
    // dependendo de onde esta em relacao a cada luz). Superficie matte
    // (Ks = 0) para um aspecto fosco, sem brilho especular.
    Objeto3D lua;
    lua.nome       = "lua";
    lua.VAO        = vaoLua;
    lua.nVertices  = nVertLua;
    lua.posicao    = vec3(-35.0f, 48.0f, -140.0f);
    lua.escala     = vec3(16.0f);     // raio aparente grande no ceu
    lua.texID      = texLua;
    lua.temTextura = (texLua != 0);
    lua.Ks         = vec3(0.0f);      // fosca: sem reflexo especular
    lua.editavel   = true;            // pode ter sua trajetoria editada (TAB/P)

    // Trajetoria padrao: orbita VERTICAL contornando o retangulo de
    // terra. Os pontos formam um circulo no plano YZ (x = 0), centrado
    // na terra (origem) — a lua sobe por cima do cenario e desce por
    // baixo, dando a volta completa em torno do retangulo de grama.
    {
        const int   N = 16;
        const float R = 130.0f;   // raio da orbita (muito maior que a terra)
        for (int i = 0; i < N; i++) {
            float a = (float)i / N * 2.0f * (float)M_PI;
            lua.trajetoria.pontos.push_back(vec3(0.0f, R * sinf(a), R * cosf(a)));
        }
        lua.trajetoria.velocidade = 35.0f;   // ~23 s por volta completa
    }
    objetos.push_back(lua);

    // ---- Caixa do Crash Bandicoot (cubo) em curva de Bezier ----
    // Reaproveita o VAO do cubo do chao, mas com a textura do caixote.
    // Sua trajetoria e uma SPLINE CUBICA DE BEZIER fechada (bezier =
    // true), suave, passando pelos 4 pontos de controle abaixo —
    // diferente da interpolacao linear usada pela lua e arvores.
    Objeto3D caixa;
    caixa.nome       = "caixa";
    caixa.VAO        = vaoCubo;
    caixa.nVertices  = nVertCubo;
    caixa.escala     = vec3(1.0f);
    caixa.texID      = texCaixa;
    caixa.temTextura = (texCaixa != 0);
    caixa.Ks         = vec3(0.1f);
    caixa.brilho     = 16.0f;
    caixa.editavel   = true;
    caixa.trajetoria.bezier     = true;
    caixa.trajetoria.velocidade = 4.0f;
    // 4 pontos de controle formando um losango acima do chao; a altura
    // alterna para a caixa subir e descer ao longo da curva.
    caixa.trajetoria.pontos = {
        vec3( 6.0f, 1.5f,  0.0f),
        vec3( 0.0f, 3.5f,  6.0f),
        vec3(-6.0f, 1.5f,  0.0f),
        vec3( 0.0f, 3.5f, -6.0f),
    };
    objetos.push_back(caixa);

    // ---- Macaco (Suzanne) girando em orbita vertical LATERAL a da lua ----
    // A lua orbita no plano YZ (x = 0): vista da camera, sobe/desce no
    // eixo frente-tras. O macaco orbita no plano XY (z = 0): vista da
    // camera, sobe pela direita e desce pela esquerda — um caminho
    // circular vertical em volta da terra, lateral ao da lua (os dois
    // planos se cruzam so no topo/baixo). Raio menor (110 vs 130) para
    // nunca coincidir com o caminho da lua. Gira em torno do eixo Y.
    Objeto3D macaco;
    macaco.nome       = "macaco";
    macaco.VAO        = vaoMacaco;
    macaco.nVertices  = nVertMacaco;
    macaco.escala     = vec3(12.0f);
    macaco.texID      = texMacaco;
    macaco.temTextura = (texMacaco != 0);
    macaco.Ks         = vec3(0.2f);
    macaco.brilho     = 32.0f;
    macaco.editavel   = true;
    macaco.spin       = vec3(0.0f, 90.0f, 0.0f);  // girante: 90 graus/seg em Y
    {
        const int   N = 16;
        const float R = 110.0f;   // < 130 da lua (caminho lateral, sem cruzar)
        for (int i = 0; i < N; i++) {
            float a = (float)i / N * 2.0f * (float)M_PI;
            macaco.trajetoria.pontos.push_back(vec3(R * cosf(a), R * sinf(a), 0.0f));
        }
        macaco.trajetoria.velocidade = 30.0f;
    }
    objetos.push_back(macaco);

    // Tenta carregar trajetorias salvas em disco; para os objetos com
    // trajetoria padrao (lua, caixa e macaco), se o arquivo nao tiver
    // pontos deles, mantemos a trajetoria definida acima.
    {
        vector<pair<string, vector<vec3>>> padroes;
        for (const Objeto3D& o : objetos)
            if (!o.trajetoria.pontos.empty())
                padroes.push_back({ o.nome, o.trajetoria.pontos });

        carregarTrajetorias(ARQUIVO_TRAJETORIAS);

        for (const auto& pr : padroes)
            for (Objeto3D& o : objetos)
                if (o.nome == pr.first && o.trajetoria.pontos.empty())
                    o.trajetoria.pontos = pr.second;
    }

    // Seleciona a caixa como objeto editavel inicial e mostra sua curva.
    for (int i = 0; i < (int)objetos.size(); i++) {
        if (objetos[i].nome == "caixa") { objetoSelecionado = i; break; }
    }
    uploadTrajetoriaVBO(objetos[objetoSelecionado].trajetoria);

    glUseProgram(programa);
    const GLint locModel    = glGetUniformLocation(programa, "model");
    const GLint locView     = glGetUniformLocation(programa, "view");
    const GLint locProj     = glGetUniformLocation(programa, "proj");
    const GLint locViewPos  = glGetUniformLocation(programa, "viewPos");
    const GLint locKa       = glGetUniformLocation(programa, "Ka");
    const GLint locKd       = glGetUniformLocation(programa, "Kd");
    const GLint locKs       = glGetUniformLocation(programa, "Ks");
    const GLint locBrilho   = glGetUniformLocation(programa, "brilho");
    const GLint locTex      = glGetUniformLocation(programa, "texDifusa");
    const GLint locUsarTex  = glGetUniformLocation(programa, "usarTextura");
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

        // Atualiza a posicao (trajetoria) e a rotacao automatica (spin)
        // de cada objeto. O macaco, por exemplo, gira enquanto orbita.
        for (Objeto3D& obj : objetos) {
            atualizaTrajetoria(obj, deltaTime);
            obj.rotacao += obj.spin * deltaTime;
        }

        // Fundo padrao (mesmo do desafio do Modulo 6).
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

        // Trajetoria do objeto selecionado por cima da cena (linhas
        // amarelas + pontos de controle em magenta).
        desenhaTrajetoria(view, proj);

        // Mira central (HUD). Roxa em modo edicao.
        desenhaCrosshair(aspect);

        glfwSwapBuffers(janela);
    }

    // O VAO do cubo e o da arvore sao compartilhados por varios
    // objetos; apagamos apenas uma vez cada.
    glDeleteVertexArrays(1, &vaoCubo);
    glDeleteVertexArrays(1, &vaoArv);
    glDeleteVertexArrays(1, &vaoLua);
    glDeleteVertexArrays(1, &vaoMacaco);
    if (texGrama)  glDeleteTextures(1, &texGrama);
    if (texArvore) glDeleteTextures(1, &texArvore);
    if (texLua)    glDeleteTextures(1, &texLua);
    if (texCaixa)  glDeleteTextures(1, &texCaixa);
    if (texMacaco) glDeleteTextures(1, &texMacaco);
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

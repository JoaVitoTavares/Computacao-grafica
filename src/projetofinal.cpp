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
// Rotacionar pelo mouse) e a iluminacao Phong do M4, agora com um
// termo de luz ambiente para dar aparencia de cena externa diurna.
//
// Controles:
//   W A S D        — andar (frente/tras/esquerda/direita)
//   Q / E          — descer / subir
//   Mouse          — olhar ao redor (yaw / pitch)
//   Scroll         — zoom (altera FOV)
//   1 / 2 / 3      — liga/desliga sol / preenchimento / fundo
//   M              — alterna solido / wireframe
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
          MovementSpeed(4.0f),
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
// Estruturas — Objeto3D, Luz
// -------------------------------------------------------------

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
};

struct Luz {
    vec3 pos;
    vec3 color;
    bool ativa = true;
};

// Tres luzes ao redor do cenario. A "principal" faz o papel de sol.
Luz luzes[3] = {
    { vec3(  8.0f, 14.0f,  10.0f), vec3(1.0f, 0.97f, 0.85f), true }, // sol (alto, frente)
    { vec3(-12.0f,  6.0f,   4.0f), vec3(0.4f, 0.55f, 0.9f),  true }, // preenchimento (ceu, lateral)
    { vec3(  0.0f,  5.0f, -16.0f), vec3(0.9f, 0.7f,  0.5f),  true }, // fundo (atras)
};

vector<Objeto3D> objetos;
bool             modoWireframe = false;

// A camera e o "tempo" precisam ser globais por causa dos callbacks
// de GLFW (que sao funcoes livres).
Camera camera;
float  deltaTime  = 0.0f;
float  lastFrame  = 0.0f;
float  lastX      = WIDTH  / 2.0f;
float  lastY      = HEIGHT / 2.0f;
bool   firstMouse = true;

// -------------------------------------------------------------
// Shaders — Phong com 3 luzes + ambiente + textura opcional.
// Suporta recorte por alpha (arvores) e modo "unlit" (billboard).
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

// Tres luzes: 0=sol  1=preenchimento  2=fundo
uniform vec3 lightPos[3];
uniform vec3 lightColor[3];
uniform int  lightEnabled[3];

uniform vec3  Ka;
uniform vec3  Kd;
uniform vec3  Ks;
uniform float brilho;

uniform bool      usarTextura;
uniform sampler2D texDifusa;
uniform vec3      luzAmbiente;

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

    // Ambiente global para aparencia de cena externa diurna.
    vec3 resultado = luzAmbiente * corDifusa;

    for (int i = 0; i < 3; i++) {
        if (lightEnabled[i] == 0) continue;

        vec3 L = normalize(lightPos[i] - fragPos);
        vec3 R = reflect(-L, N);

        float diff      = max(dot(N, L), 0.0);
        vec3  difusa    = diff * lightColor[i] * corDifusa;

        float spec      = pow(max(dot(V, R), 0.0), brilho);
        vec3  especular = Ks * spec * lightColor[i];

        resultado += difusa + especular;
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
    // Normais "para fora" sao irrelevantes (a arvore e unlit), mas
    // preenchemos com algo coerente por consistencia do layout.
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
    glUniform3f(glGetUniformLocation(programaCrosshair, "cor"), 1.0f, 1.0f, 1.0f);
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
    Objeto3D arvore;
    arvore.nome       = "arvore";
    arvore.VAO        = vaoArv;
    arvore.nVertices  = nVertArv;
    arvore.posicao    = vec3(x, 0.0f, z);
    arvore.escala     = vec3(escala);
    arvore.texID      = texArv;
    arvore.temTextura = (texArv != 0);
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
        const char* nomes[] = { "sol", "preenchimento", "fundo" };
        cout << "Luz " << nomes[idx]
             << (luzes[idx].ativa ? ": LIGADA" : ": DESLIGADA") << endl;
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
    cout << "1/2/3      : sol / preenchimento / fundo on-off" << endl;
    cout << "M          : wireframe | ESC: sair" << endl;
    cout << "=================================" << endl;

    GLuint programa = linkaPrograma(vertexShaderSrc, fragmentShaderSrc);
    inicializaCrosshair();
    glEnable(GL_DEPTH_TEST);

    // ---- Recursos compartilhados do cenario ----
    int    nVertCubo = 0, nVertArv = 0;
    GLuint vaoCubo   = criaCuboTexturizado(nVertCubo);
    GLuint vaoArv    = criaArvoreBillboard(/*largura=*/2.4f, /*altura=*/3.6f, nVertArv);

    GLuint texGrama  = loadTexture(resolvePath("assets/tex/grass.png"), /*pixelArt=*/true);
    GLuint texArvore = loadTexture(resolvePath("assets/tex/tree.png"),  /*pixelArt=*/true);

    // ---- Base de blocos de grama ----
    montarChao(vaoCubo, nVertCubo, texGrama);

    // ---- Arvores sobre os blocos ----
    plantarArvore(vaoArv, nVertArv, texArvore, -4.0f, -3.0f, 1.0f);
    plantarArvore(vaoArv, nVertArv, texArvore,  3.0f, -1.0f, 1.2f);
    plantarArvore(vaoArv, nVertArv, texArvore, -1.0f,  3.0f, 0.9f);
    plantarArvore(vaoArv, nVertArv, texArvore,  5.0f,  4.0f, 1.1f);
    plantarArvore(vaoArv, nVertArv, texArvore,  0.0f, -5.0f, 1.0f);
    plantarArvore(vaoArv, nVertArv, texArvore, -5.0f,  1.0f, 0.85f);

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
    const GLint locAmbiente = glGetUniformLocation(programa, "luzAmbiente");
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
        glUniform3f(locAmbiente, 0.35f, 0.37f, 0.40f);

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

        // Mira central (HUD).
        desenhaCrosshair(aspect);

        glfwSwapBuffers(janela);
    }

    // O VAO do cubo e o da arvore sao compartilhados por varios
    // objetos; apagamos apenas uma vez cada.
    glDeleteVertexArrays(1, &vaoCubo);
    glDeleteVertexArrays(1, &vaoArv);
    if (texGrama)  glDeleteTextures(1, &texGrama);
    if (texArvore) glDeleteTextures(1, &texArvore);
    if (vboCrosshair)      glDeleteBuffers(1, &vboCrosshair);
    if (vaoCrosshair)      glDeleteVertexArrays(1, &vaoCrosshair);
    if (programaCrosshair) glDeleteProgram(programaCrosshair);
    glDeleteProgram(programa);
    glfwTerminate();
    return 0;
}

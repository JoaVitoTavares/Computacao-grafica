// =============================================================
// Desafio Modulo 5 — Camera em Primeira Pessoa
//
// Camera sintetica com projecao perspectiva e navegacao em 1a
// pessoa, encapsulada em uma classe Camera com metodos Mover
// (WASD/QE) e Rotacionar (mouse), conforme o material de
// aprofundamento (M5).
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
// Controles:
//   W A S D        — andar (frente/tras/esquerda/direita)
//   Q / E          — descer / subir
//   Mouse          — olhar ao redor (yaw / pitch)
//   Scroll         — zoom (altera FOV)
//   1 / 2 / 3      — liga/desliga luz principal / preenchimento / fundo
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
bool             modoWireframe = false;

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

// Ambiente global da cena — luz indireta/dispersa, nao pertence a
// nenhuma luz especifica. Mantida baixa para que cada luz pontual
// so contribua para a face que ela realmente atinge.
const vec3 ambienteGlobal = vec3(0.15);

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPos);

    vec3 corDifusa = usarTextura ? texture(texDifusa, fragUV).rgb : Kd;

    // Ambiente: parcela unica, fora do loop, para nao multiplicar
    // por cada luz e fazer a luz "de tras" iluminar a frente.
    vec3 resultado = Ka * ambienteGlobal * corDifusa;

    for (int i = 0; i < 3; i++) {
        if (lightEnabled[i] == 0) continue;

        vec3 L = normalize(lightPos[i] - fragPos);
        vec3 R = reflect(-L, N);

        // Difusa e especular sao direcionais (dependem de N.L / V.R),
        // entao a luz so contribui na face que ela realmente atinge.
        float diff     = max(dot(N, L), 0.0);
        vec3  difusa   = diff * lightColor[i] * corDifusa;

        float spec     = pow(max(dot(V, R), 0.0), brilho);
        vec3  especular = Ks * spec * lightColor[i];

        resultado += difusa + especular;
    }

    corFinal = vec4(resultado, 1.0);
}
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

static GLuint criaPrograma() {
    GLuint vs = compilaShader(vertexShaderSrc,   GL_VERTEX_SHADER);
    GLuint fs = compilaShader(fragmentShaderSrc, GL_FRAGMENT_SHADER);
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

GLuint loadTexture(const string& filePath) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int w, h, canais;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &canais, 0);
    if (data) {
        GLenum formato = (canais == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, formato, w, h, 0, formato, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura carregada: " << filePath << " (" << w << "x" << h << ")" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID); texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
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
    if (action == GLFW_PRESS) {
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
        }
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
        "Desafio Modulo 5 - Camera 1a Pessoa + Phong Multiluz", nullptr, nullptr);
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
    cout << "Mouse      : olhar ao redor" << endl;
    cout << "Scroll     : zoom (FOV)" << endl;
    cout << "1/2/3      : luz principal / preenchimento / fundo on-off" << endl;
    cout << "M / ESC    : wireframe / sair" << endl;
    cout << "=================================" << endl;

    GLuint programa = criaPrograma();
    glEnable(GL_DEPTH_TEST);

    // Cena: Suzanne a esquerda, Cubo a direita, Suzanne extra ao fundo.
    // Material (Ka/Kd/Ks/Ns) lido do .MTL de cada objeto.
    Objeto3D suzanne = criaObjeto("Suzanne", "assets/Modelos3D/Suzanne.obj");
    suzanne.posicao  = vec3(-2.0f, 0.0f, 0.0f);
    if (suzanne.VAO) objetos.push_back(suzanne);

    Objeto3D cubo = criaObjeto("Cubo", "assets/Modelos3D/Cube.obj");
    cubo.posicao  = vec3(2.0f, 0.0f, 0.0f);
    if (cubo.VAO) objetos.push_back(cubo);

    Objeto3D suzanneFundo = criaObjeto("SuzanneFundo", "assets/Modelos3D/Suzanne.obj");
    suzanneFundo.posicao  = vec3(0.0f, 1.0f, -5.0f);
    if (suzanneFundo.VAO) objetos.push_back(suzanneFundo);

    if (objetos.empty()) {
        cerr << "Nenhum objeto carregado. Verifique assets/Modelos3D/." << endl;
        glfwTerminate();
        return -1;
    }

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

        glfwSwapBuffers(janela);
    }

    for (Objeto3D& obj : objetos) {
        if (obj.VAO)   glDeleteVertexArrays(1, &obj.VAO);
        if (obj.texID) glDeleteTextures(1, &obj.texID);
    }
    glDeleteProgram(programa);
    glfwTerminate();
    return 0;
}

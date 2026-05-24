// =============================================================
// Desafio Modulo 5 — Camera em Primeira Pessoa
//
// Implementa uma camera sintetica com projecao perspectiva e
// navegacao em 1a pessoa, encapsulada em uma classe Camera, com
// metodos Mover (WASD) e Rotacionar (mouse), conforme o material
// de aprofundamento (M5).
//
// Controles:
//   W A S D        — movimenta a camera (frente/tras/esquerda/direita)
//   Q / E          — desce / sobe (movimento vertical absoluto)
//   Mouse          — olha ao redor (yaw / pitch)
//   Scroll         — zoom (altera FOV)
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
// Objeto3D + estado global
// -------------------------------------------------------------

struct Objeto3D {
    GLuint VAO        = 0;
    int    nVertices  = 0;
    string nome;
    vec3   posicao    = vec3(0.0f);
    vec3   rotacao    = vec3(0.0f);
    vec3   escala     = vec3(1.0f);
    vec3   cor        = vec3(0.8f);
    float  Ka         = 0.15f;
    float  Kd         = 0.7f;
    float  Ks         = 0.5f;
    float  brilho     = 32.0f;
    GLuint texID      = 0;
    bool   temTextura = false;
};

vector<Objeto3D> objetos;
bool             modoWireframe = false;

// A camera e o "tempo" precisam ser globais para serem acessados
// pelos callbacks do GLFW (que sao funcoes livres).
Camera camera(vec3(0.0f, 1.5f, 7.0f));
float  deltaTime = 0.0f;
float  lastFrame = 0.0f;
float  lastX     = WIDTH  / 2.0f;
float  lastY     = HEIGHT / 2.0f;
bool   firstMouse = true;

// -------------------------------------------------------------
// Shaders — Phong com suporte a textura
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

uniform vec3  lightPos;
uniform vec3  viewPos;
uniform vec3  lightColor;
uniform vec3  objectColor;
uniform float Ka;
uniform float Kd;
uniform float Ks;
uniform float brilho;

uniform bool      usarTextura;
uniform sampler2D texDifusa;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(lightPos - fragPos);
    vec3 V = normalize(viewPos  - fragPos);
    vec3 R = reflect(-L, N);

    vec3 baseColor = usarTextura ? texture(texDifusa, fragUV).rgb : objectColor;

    vec3  ambient  = Ka * lightColor;
    float diff     = max(dot(N, L), 0.0);
    vec3  diffuse  = Kd * diff * lightColor;
    float spec     = pow(max(dot(V, R), 0.0), brilho);
    vec3  specular = Ks * spec * lightColor;

    corFinal = vec4((ambient + diffuse) * baseColor + specular, 1.0);
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
// Utilitarios de caminho — permite rodar da raiz ou de build/
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
// loadMTL / loadTexture / loadSimpleOBJ
// (mesma logica do desafio do modulo 3)
// -------------------------------------------------------------

string loadMTL(string filePath) {
    ifstream arquivo(filePath);
    if (!arquivo.is_open()) return "";
    string linha;
    while (getline(arquivo, linha)) {
        istringstream iss(linha);
        string prefixo; iss >> prefixo;
        if (prefixo == "map_Kd") { string nome; iss >> nome; return nome; }
    }
    return "";
}

GLuint loadTexture(string filePath) {
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

int loadSimpleOBJ(string filePath, int &nVertices, string &mtlFilename) {
    ifstream arquivo(filePath);
    if (!arquivo.is_open()) {
        cerr << "Erro ao tentar ler o arquivo " << filePath << endl;
        return -1;
    }

    vector<vec3>    vertices;
    vector<vec2>    texCoords;
    vector<vec3>    normals;
    vector<GLfloat> vBuffer;
    mtlFilename.clear();

    string linha;
    while (getline(arquivo, linha)) {
        if (linha.empty() || linha[0] == '#') continue;
        istringstream iss(linha);
        string tag; iss >> tag;

        if (tag == "v")        { vec3 v; iss >> v.x >> v.y >> v.z; vertices.push_back(v); }
        else if (tag == "vt")  { vec2 t; iss >> t.x >> t.y;        texCoords.push_back(t); }
        else if (tag == "vn")  { vec3 n; iss >> n.x >> n.y >> n.z; normals.push_back(n); }
        else if (tag == "mtllib") { iss >> mtlFilename; }
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

static Objeto3D criaObjeto(const string& nome, const string& objRel,
                            vec3 corFallback,
                            float Ka, float Kd, float Ks, float brilho) {
    Objeto3D obj;
    obj.nome = nome; obj.cor = corFallback;
    obj.Ka = Ka; obj.Kd = Kd; obj.Ks = Ks; obj.brilho = brilho;

    string objPath = resolvePath(objRel);
    int n = 0; string mtlFile;
    int vao = loadSimpleOBJ(objPath, n, mtlFile);
    if (vao < 0) return obj;

    obj.VAO = (GLuint)vao; obj.nVertices = n;

    if (!mtlFile.empty()) {
        string dir     = diretorioDe(objPath);
        string mtlPath = dir + mtlFile;
        string mapKd   = loadMTL(mtlPath);
        if (!mapKd.empty()) {
            string texPath = dir + mapKd;
            if (arquivoExiste(texPath)) {
                obj.texID      = loadTexture(texPath);
                obj.temTextura = (obj.texID != 0);
            }
        }
    }
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

// Processa teclas mantidas pressionadas (movimentacao continua).
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
        "Desafio Modulo 5 - Camera em Primeira Pessoa", nullptr, nullptr);
    if (!janela) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(janela);
    glfwSetKeyCallback(janela, key_callback);
    glfwSetCursorPosCallback(janela, mouse_callback);
    glfwSetScrollCallback(janela, scroll_callback);
    glfwSetFramebufferSizeCallback(janela, framebuffer_callback);

    // Captura o cursor para a navegacao em 1a pessoa (mouse look).
    glfwSetInputMode(janela, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Erro ao inicializar GLAD" << endl;
        return -1;
    }

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;
    cout << "\n=== Controles ===" << endl;
    cout << "WASD       : mover | Q/E : descer/subir" << endl;
    cout << "Mouse      : olhar ao redor" << endl;
    cout << "Scroll     : zoom (FOV)" << endl;
    cout << "M / ESC    : wireframe / sair" << endl;

    GLuint programa = criaPrograma();
    glEnable(GL_DEPTH_TEST);

    // Cena: Suzanne a esquerda, Cubo a direita, outra Suzanne ao fundo.
    Objeto3D suzanne = criaObjeto("Suzanne", "assets/Modelos3D/Suzanne.obj",
                                  vec3(0.8f, 0.6f, 0.3f), 0.15f, 0.8f, 0.5f, 32.0f);
    suzanne.posicao = vec3(-2.0f, 0.0f, 0.0f);
    if (suzanne.VAO) objetos.push_back(suzanne);

    Objeto3D cubo = criaObjeto("Cubo", "assets/Modelos3D/Cube.obj",
                               vec3(0.4f, 0.7f, 0.9f), 0.15f, 0.7f, 0.6f, 48.0f);
    cubo.posicao = vec3(2.0f, 0.0f, 0.0f);
    if (cubo.VAO) objetos.push_back(cubo);

    Objeto3D suzanne2 = criaObjeto("SuzanneFundo", "assets/Modelos3D/Suzanne.obj",
                                   vec3(0.6f, 0.3f, 0.8f), 0.15f, 0.8f, 0.5f, 32.0f);
    suzanne2.posicao = vec3(0.0f, 1.0f, -5.0f);
    if (suzanne2.VAO) objetos.push_back(suzanne2);

    if (objetos.empty()) {
        cerr << "Nenhum objeto carregado. Verifique assets/Modelos3D/." << endl;
        glfwTerminate();
        return -1;
    }

    glUseProgram(programa);
    const GLint locModel       = glGetUniformLocation(programa, "model");
    const GLint locView        = glGetUniformLocation(programa, "view");
    const GLint locProj        = glGetUniformLocation(programa, "proj");
    const GLint locViewPos     = glGetUniformLocation(programa, "viewPos");
    const GLint locLightPos    = glGetUniformLocation(programa, "lightPos");
    const GLint locLightColor  = glGetUniformLocation(programa, "lightColor");
    const GLint locObjectColor = glGetUniformLocation(programa, "objectColor");
    const GLint locKa          = glGetUniformLocation(programa, "Ka");
    const GLint locKd          = glGetUniformLocation(programa, "Kd");
    const GLint locKs          = glGetUniformLocation(programa, "Ks");
    const GLint locBrilho      = glGetUniformLocation(programa, "brilho");
    const GLint locTex         = glGetUniformLocation(programa, "texDifusa");
    const GLint locUsarTex     = glGetUniformLocation(programa, "usarTextura");
    glUniform1i(locTex, 0);

    const vec3 lightPos(3.0f, 4.0f, 3.0f);

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
        glUniform3fv(locViewPos,    1, value_ptr(camera.Position));
        glUniform3fv(locLightPos,   1, value_ptr(lightPos));
        glUniform3f (locLightColor, 1.0f, 1.0f, 1.0f);

        for (Objeto3D& obj : objetos) {
            mat4 model(1.0f);
            model = translate(model, obj.posicao);
            model = rotate(model, radians(obj.rotacao.x), vec3(1, 0, 0));
            model = rotate(model, radians(obj.rotacao.y), vec3(0, 1, 0));
            model = rotate(model, radians(obj.rotacao.z), vec3(0, 0, 1));
            model = scale(model, obj.escala);
            glUniformMatrix4fv(locModel, 1, GL_FALSE, value_ptr(model));

            glUniform3fv(locObjectColor, 1, value_ptr(obj.cor));
            glUniform1f (locKa,     obj.Ka);
            glUniform1f (locKd,     obj.Kd);
            glUniform1f (locKs,     obj.Ks);
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

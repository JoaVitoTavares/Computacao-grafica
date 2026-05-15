#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;
using namespace glm;

const int WIDTH  = 1000;
const int HEIGHT = 700;

enum class ModoTransformacao { TRANSLACAO, ROTACAO, ESCALA };

struct Objeto3D {
    GLuint   VAO       = 0;
    GLuint   VBO       = 0;
    GLsizei  nVertices = 0;
    string   nome;
    vec3     posicao   = vec3(0.0f);
    vec3     rotacao   = vec3(0.0f);
    vec3     escala    = vec3(1.0f);
    vec3     cor       = vec3(0.8f);
    float    Ka        = 0.15f;
    float    Kd        = 0.7f;
    float    Ks        = 0.5f;
    float    brilho    = 32.0f;
};

vector<Objeto3D>   objetos;
int                objetoSelecionado = 0;
ModoTransformacao  modoAtual         = ModoTransformacao::TRANSLACAO;
bool               modoWireframe     = false;

GLFWwindow* janela  = nullptr;
GLuint      programa = 0;

// ============================================================
// Shaders – iluminação de Phong (ambiente + difusa + especular)
// ============================================================

const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 posicao;
layout(location = 1) in vec3 normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 fragPos;
out vec3 fragNormal;

void main() {
    vec4 worldPos = model * vec4(posicao, 1.0);
    fragPos    = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(model))) * normal;
    gl_Position = proj * view * worldPos;
}
)";

const char* fragmentShaderSrc = R"(
#version 330 core
in vec3 fragPos;
in vec3 fragNormal;

out vec4 corFinal;

uniform vec3  lightPos;
uniform vec3  viewPos;
uniform vec3  lightColor;
uniform vec3  objectColor;
uniform float Ka;
uniform float Kd;
uniform float Ks;
uniform float brilho;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(lightPos - fragPos);
    vec3 V = normalize(viewPos  - fragPos);
    vec3 R = reflect(-L, N);

    vec3  ambient  = Ka * lightColor;
    float diff     = max(dot(N, L), 0.0);
    vec3  diffuse  = Kd * diff * lightColor;
    float spec     = pow(max(dot(V, R), 0.0), brilho);
    vec3  specular = Ks * spec * lightColor;

    corFinal = vec4((ambient + diffuse) * objectColor + specular, 1.0);
}
)";

// ============================================================
// Utilitários de geometria
// ============================================================

static void pushVertice(vector<float>& v, vec3 pos, vec3 n)
{
    v.insert(v.end(), { pos.x, pos.y, pos.z, n.x, n.y, n.z });
}

static void pushTriangulo(vector<float>& v, vec3 a, vec3 b, vec3 c, vec3 n)
{
    pushVertice(v, a, n);
    pushVertice(v, b, n);
    pushVertice(v, c, n);
}

static Objeto3D criaObjeto(const string& nome, const vector<float>& dados,
                            vec3 cor, float Ka, float Kd, float Ks, float brilho)
{
    Objeto3D obj;
    obj.nome      = nome;
    obj.cor       = cor;
    obj.Ka        = Ka;
    obj.Kd        = Kd;
    obj.Ks        = Ks;
    obj.brilho    = brilho;
    obj.nVertices = (GLsizei)(dados.size() / 6);

    glGenVertexArrays(1, &obj.VAO);
    glGenBuffers(1, &obj.VBO);

    glBindVertexArray(obj.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, obj.VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(dados.size() * sizeof(float)),
                 dados.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    cout << "Objeto criado: " << nome << " | vertices: " << obj.nVertices << endl;
    return obj;
}

// ============================================================
// Objetos procedurais
// ============================================================

// Esfera UV — normal = posição normalizada na esfera unitária
static Objeto3D criaEsfera(int stacks, int slices)
{
    vector<float> dados;
    const float PI = glm::pi<float>();

    for (int i = 0; i < stacks; i++) {
        float phi1 = PI * i       / stacks - PI / 2.0f;
        float phi2 = PI * (i + 1) / stacks - PI / 2.0f;

        for (int j = 0; j < slices; j++) {
            float theta1 = 2.0f * PI * j       / slices;
            float theta2 = 2.0f * PI * (j + 1) / slices;

            auto vert = [](float phi, float theta) -> vec3 {
                return vec3(cos(phi) * cos(theta),
                            sin(phi),
                            cos(phi) * sin(theta));
            };

            vec3 v00 = vert(phi1, theta1);
            vec3 v10 = vert(phi2, theta1);
            vec3 v01 = vert(phi1, theta2);
            vec3 v11 = vert(phi2, theta2);

            // Na esfera unitária a normal é igual à posição
            pushVertice(dados, v00, v00);
            pushVertice(dados, v10, v10);
            pushVertice(dados, v11, v11);

            pushVertice(dados, v00, v00);
            pushVertice(dados, v11, v11);
            pushVertice(dados, v01, v01);
        }
    }

    return criaObjeto("Esfera", dados,
                       vec3(0.3f, 0.8f, 0.4f),
                       0.15f, 0.8f, 0.6f, 64.0f);
}

// Pirâmide de base quadrada
static Objeto3D criaPiramide()
{
    vector<float> dados;
    const float h = 0.5f;
    const float s = 0.5f;

    vec3 apex(0.0f,  h, 0.0f);
    vec3 b0(-s, -h, -s);
    vec3 b1( s, -h, -s);
    vec3 b2( s, -h,  s);
    vec3 b3(-s, -h,  s);

    auto faceNormal = [](vec3 a, vec3 b, vec3 c) {
        return normalize(cross(b - a, c - a));
    };

    // Quatro faces laterais
    pushTriangulo(dados, b0, b1, apex, faceNormal(b0, b1, apex));
    pushTriangulo(dados, b1, b2, apex, faceNormal(b1, b2, apex));
    pushTriangulo(dados, b2, b3, apex, faceNormal(b2, b3, apex));
    pushTriangulo(dados, b3, b0, apex, faceNormal(b3, b0, apex));

    // Base (dois triângulos, normal para baixo)
    vec3 nBase(0.0f, -1.0f, 0.0f);
    pushTriangulo(dados, b0, b3, b2, nBase);
    pushTriangulo(dados, b0, b2, b1, nBase);

    return criaObjeto("Piramide", dados,
                       vec3(1.0f, 0.6f, 0.1f),
                       0.15f, 0.7f, 0.4f, 16.0f);
}

// Cilindro — tampa superior, inferior e lateral
static Objeto3D criaCilindro(int slices)
{
    vector<float> dados;
    const float PI = glm::pi<float>();
    const float h  = 0.5f;

    for (int i = 0; i < slices; i++) {
        float t1 = 2.0f * PI * i       / slices;
        float t2 = 2.0f * PI * (i + 1) / slices;

        float x1 = cos(t1), z1 = sin(t1);
        float x2 = cos(t2), z2 = sin(t2);

        // Lateral: normais apontam para fora em cada vértice
        pushVertice(dados, vec3(x1, -h, z1), vec3(x1, 0, z1));
        pushVertice(dados, vec3(x1,  h, z1), vec3(x1, 0, z1));
        pushVertice(dados, vec3(x2,  h, z2), vec3(x2, 0, z2));

        pushVertice(dados, vec3(x1, -h, z1), vec3(x1, 0, z1));
        pushVertice(dados, vec3(x2,  h, z2), vec3(x2, 0, z2));
        pushVertice(dados, vec3(x2, -h, z2), vec3(x2, 0, z2));

        // Tampa superior
        vec3 nTop(0, 1, 0);
        pushTriangulo(dados,
                      vec3(0,  h, 0),
                      vec3(x1, h, z1),
                      vec3(x2, h, z2), nTop);

        // Tampa inferior
        vec3 nBot(0, -1, 0);
        pushTriangulo(dados,
                      vec3(0,  -h, 0),
                      vec3(x2, -h, z2),
                      vec3(x1, -h, z1), nBot);
    }

    return criaObjeto("Cilindro", dados,
                       vec3(0.5f, 0.3f, 0.9f),
                       0.15f, 0.7f, 0.9f, 128.0f);
}

// ============================================================
// Shaders
// ============================================================

static GLuint compilaShader(const char* src, GLenum tipo)
{
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

static void inicializaShaders()
{
    GLuint vs = compilaShader(vertexShaderSrc,   GL_VERTEX_SHADER);
    GLuint fs = compilaShader(fragmentShaderSrc, GL_FRAGMENT_SHADER);

    programa = glCreateProgram();
    glAttachShader(programa, vs);
    glAttachShader(programa, fs);
    glLinkProgram(programa);

    GLint ok;
    glGetProgramiv(programa, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(programa, 512, nullptr, log);
        cerr << "Shader link error: " << log << endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

// ============================================================
// Input
// ============================================================

static void imprimirControles()
{
    cout << "\n================= CONTROLES =================" << endl;
    cout << "ESC              fechar" << endl;
    cout << "TAB              selecionar proximo objeto" << endl;
    cout << "T                modo translacao" << endl;
    cout << "R                modo rotacao" << endl;
    cout << "S                modo escala" << endl;
    cout << "M                alternar solido/wireframe" << endl;
    cout << "" << endl;
    cout << "TRANSLACAO:" << endl;
    cout << "  Setas          move em X/Y" << endl;
    cout << "  PgUp/PgDn      move em Z" << endl;
    cout << "  X / Y / Z      move no eixo" << endl;
    cout << "  Shift+X/Y/Z    sentido negativo" << endl;
    cout << "" << endl;
    cout << "ROTACAO / ESCALA:" << endl;
    cout << "  X / Y / Z      eixo (Shift = negativo)" << endl;
    cout << "  + / -          escala uniforme" << endl;
    cout << "=============================================" << endl;
}

static void imprimirSelecionado()
{
    if (objetos.empty()) return;
    cout << "Selecionado: [" << objetoSelecionado << "] "
         << objetos[objetoSelecionado].nome << endl;
}

static void applyTransformation(int key, int mods)
{
    if (objetos.empty()) return;
    Objeto3D& obj = objetos[objetoSelecionado];

    const float dir  = (mods & GLFW_MOD_SHIFT) ? -1.0f : 1.0f;
    const float tStp = 0.1f;
    const float rStp = 5.0f;
    const float sStp = 0.1f;
    const float sMin = 0.05f;

    if (modoAtual == ModoTransformacao::TRANSLACAO) {
        if      (key == GLFW_KEY_LEFT)       obj.posicao.x -= tStp;
        else if (key == GLFW_KEY_RIGHT)      obj.posicao.x += tStp;
        else if (key == GLFW_KEY_UP)         obj.posicao.y += tStp;
        else if (key == GLFW_KEY_DOWN)       obj.posicao.y -= tStp;
        else if (key == GLFW_KEY_PAGE_UP)    obj.posicao.z += tStp;
        else if (key == GLFW_KEY_PAGE_DOWN)  obj.posicao.z -= tStp;
        else if (key == GLFW_KEY_X)          obj.posicao.x += dir * tStp;
        else if (key == GLFW_KEY_Y)          obj.posicao.y += dir * tStp;
        else if (key == GLFW_KEY_Z)          obj.posicao.z += dir * tStp;
    }
    else if (modoAtual == ModoTransformacao::ROTACAO) {
        if      (key == GLFW_KEY_X) obj.rotacao.x += dir * rStp;
        else if (key == GLFW_KEY_Y) obj.rotacao.y += dir * rStp;
        else if (key == GLFW_KEY_Z) obj.rotacao.z += dir * rStp;
    }
    else {
        if      (key == GLFW_KEY_EQUAL   || key == GLFW_KEY_KP_ADD)      obj.escala += vec3(sStp);
        else if (key == GLFW_KEY_MINUS   || key == GLFW_KEY_KP_SUBTRACT) obj.escala -= vec3(sStp);
        else if (key == GLFW_KEY_X) obj.escala.x += dir * sStp;
        else if (key == GLFW_KEY_Y) obj.escala.y += dir * sStp;
        else if (key == GLFW_KEY_Z) obj.escala.z += dir * sStp;
        obj.escala.x = std::max(obj.escala.x, sMin);
        obj.escala.y = std::max(obj.escala.y, sMin);
        obj.escala.z = std::max(obj.escala.z, sMin);
    }
}

static void key_callback(GLFWwindow* window, int key, int, int action, int mods)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_ESCAPE) { glfwSetWindowShouldClose(window, true); return; }

    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        objetoSelecionado = (objetoSelecionado + 1) % (int)objetos.size();
        imprimirSelecionado();
        return;
    }
    if (key == GLFW_KEY_T && action == GLFW_PRESS) {
        modoAtual = ModoTransformacao::TRANSLACAO;
        cout << "Modo: TRANSLACAO" << endl; return;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        modoAtual = ModoTransformacao::ROTACAO;
        cout << "Modo: ROTACAO" << endl; return;
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS) {
        modoAtual = ModoTransformacao::ESCALA;
        cout << "Modo: ESCALA" << endl; return;
    }
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        modoWireframe = !modoWireframe;
        cout << (modoWireframe ? "Wireframe" : "Solido") << endl; return;
    }

    applyTransformation(key, mods);
}

static void redimensionaCallback(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
}

// ============================================================
// main
// ============================================================

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    janela = glfwCreateWindow(WIDTH, HEIGHT,
                              "Atividade Vivencial - Esfera, Piramide, Cilindro",
                              nullptr, nullptr);
    if (!janela) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(janela);
    glfwSetKeyCallback(janela, key_callback);
    glfwSetFramebufferSizeCallback(janela, redimensionaCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Erro ao inicializar GLAD" << endl;
        return -1;
    }

    cout << "Renderer: " << glGetString(GL_RENDERER) << endl;
    cout << "OpenGL:   " << glGetString(GL_VERSION)  << endl;

    inicializaShaders();
    glEnable(GL_DEPTH_TEST);

    // Três objetos posicionados lado a lado
    Objeto3D esfera = criaEsfera(24, 36);
    esfera.posicao  = vec3(-2.5f, 0.0f, 0.0f);
    objetos.push_back(esfera);

    Objeto3D piramide = criaPiramide();
    piramide.posicao  = vec3(0.0f, 0.0f, 0.0f);
    objetos.push_back(piramide);

    Objeto3D cilindro = criaCilindro(36);
    cilindro.posicao  = vec3(2.5f, 0.0f, 0.0f);
    objetos.push_back(cilindro);

    imprimirControles();
    imprimirSelecionado();

    const vec3 camPos(0.0f, 1.5f, 7.0f);
    const vec3 lightPos(3.0f, 4.0f, 3.0f);

    while (!glfwWindowShouldClose(janela)) {
        glfwPollEvents();

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, modoWireframe ? GL_LINE : GL_FILL);

        glUseProgram(programa);

        int fw, fh;
        glfwGetFramebufferSize(janela, &fw, &fh);
        float aspect = (fw > 0 && fh > 0) ? (float)fw / fh : 1.0f;

        mat4 view = lookAt(camPos, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 proj = perspective(radians(45.0f), aspect, 0.1f, 100.0f);

        glUniformMatrix4fv(glGetUniformLocation(programa, "view"),
                           1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(programa, "proj"),
                           1, GL_FALSE, value_ptr(proj));
        glUniform3fv(glGetUniformLocation(programa, "viewPos"),
                     1, value_ptr(camPos));
        glUniform3fv(glGetUniformLocation(programa, "lightPos"),
                     1, value_ptr(lightPos));
        glUniform3f(glGetUniformLocation(programa, "lightColor"),
                    1.0f, 1.0f, 1.0f);

        for (int i = 0; i < (int)objetos.size(); i++) {
            Objeto3D& obj = objetos[i];

            mat4 model(1.0f);
            model = translate(model, obj.posicao);
            model = rotate(model, radians(obj.rotacao.x), vec3(1, 0, 0));
            model = rotate(model, radians(obj.rotacao.y), vec3(0, 1, 0));
            model = rotate(model, radians(obj.rotacao.z), vec3(0, 0, 1));
            model = scale(model, obj.escala);

            glUniformMatrix4fv(glGetUniformLocation(programa, "model"),
                               1, GL_FALSE, value_ptr(model));

            // Objeto selecionado fica em destaque (vermelho-pink)
            vec3 cor = (i == objetoSelecionado) ? vec3(1.0f, 0.2f, 0.4f) : obj.cor;
            glUniform3fv(glGetUniformLocation(programa, "objectColor"),
                         1, value_ptr(cor));
            glUniform1f(glGetUniformLocation(programa, "Ka"),     obj.Ka);
            glUniform1f(glGetUniformLocation(programa, "Kd"),     obj.Kd);
            glUniform1f(glGetUniformLocation(programa, "Ks"),     obj.Ks);
            glUniform1f(glGetUniformLocation(programa, "brilho"), obj.brilho);

            glBindVertexArray(obj.VAO);
            glDrawArrays(GL_TRIANGLES, 0, obj.nVertices);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(janela);
    }

    for (Objeto3D& obj : objetos) {
        if (obj.VAO) glDeleteVertexArrays(1, &obj.VAO);
        if (obj.VBO) glDeleteBuffers(1, &obj.VBO);
    }
    glDeleteProgram(programa);
    glfwTerminate();
    return 0;
}

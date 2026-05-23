// =============================================================
// Atividade Vivencial 2 — Modulo 4
// Iluminacao de Phong com leitura de OBJ + MTL
//
// Implementa o modelo de Phong completo:
//   - Normais (vn) lidas do .OBJ e enviadas ao shader via VAO
//   - Coeficientes Ka, Kd, Ks e Ns lidos do .MTL
//   - Parcelas ambiente, difusa e especular calculadas no fragment shader
//
// Layout do VAO: pos(3) + normal(3) + uv(2) = 8 floats por vertice
// =============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
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
// Estruturas
// -------------------------------------------------------------

struct MaterialMTL {
    vec3  Ka     = vec3(0.2f);   // coeficiente ambiente
    vec3  Kd     = vec3(0.8f);   // coeficiente difuso
    vec3  Ks     = vec3(0.5f);   // coeficiente especular
    float Ns     = 32.0f;        // expoente especular (brilho)
    string mapKd;                // nome do arquivo de textura difusa
};

struct Objeto3D {
    GLuint  VAO        = 0;
    int     nVertices  = 0;
    string  nome;
    vec3    posicao    = vec3(0.0f);
    vec3    rotacao    = vec3(0.0f);
    vec3    escala     = vec3(1.0f);
    // Material lido do .MTL
    vec3    Ka         = vec3(0.2f);
    vec3    Kd         = vec3(0.8f);
    vec3    Ks         = vec3(0.5f);
    float   brilho     = 32.0f;
    GLuint  texID      = 0;
    bool    temTextura = false;
};

struct Luz {
    vec3 pos;
    vec3 color;
    bool ativa = true;
};

// Tres luzes: principal, preenchimento, fundo
// Ativadas/desativadas com as teclas 1, 2 e 3
Luz luzes[3] = {
    { vec3( 3.0f,  4.0f,  3.0f), vec3(1.0f,  1.0f,  0.95f), true  }, // principal
    { vec3(-3.0f,  2.0f,  3.0f), vec3(0.4f,  0.6f,  1.0f),  true  }, // preenchimento
    { vec3(-1.0f,  5.0f, -4.0f), vec3(1.0f,  0.8f,  0.5f),  true  }, // fundo
};

enum class ModoTransformacao { TRANSLACAO, ROTACAO, ESCALA };

vector<Objeto3D>   objetos;
int                objetoSelecionado = 0;
ModoTransformacao  modoAtual         = ModoTransformacao::TRANSLACAO;
bool               modoWireframe     = false;

GLFWwindow* janela   = nullptr;
GLuint      programa = 0;

// -------------------------------------------------------------
// Shaders — Phong com suporte a textura
// Layout do VAO: location 0 = pos, 1 = normal, 2 = uv
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
    // Matriz normal: transposta da inversa da model (corrige distorcoes de escala)
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

// Coeficientes do material (lidos do .MTL)
uniform vec3  Ka;
uniform vec3  Kd;
uniform vec3  Ks;
uniform float brilho;

// Textura difusa (opcional)
uniform bool      usarTextura;
uniform sampler2D texDifusa;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPos);

    // Cor difusa base: textura ou Kd do material
    vec3 corDifusa = usarTextura ? texture(texDifusa, fragUV).rgb : Kd;

    vec3 resultado = vec3(0.0);

    for (int i = 0; i < 3; i++) {
        if (lightEnabled[i] == 0) continue;

        vec3  L = normalize(lightPos[i] - fragPos);
        vec3  R = reflect(-L, N);

        // Parcela ambiente
        vec3 ambiente = Ka * lightColor[i];

        // Parcela difusa
        float diff  = max(dot(N, L), 0.0);
        vec3 difusa = diff * lightColor[i] * corDifusa;

        // Parcela especular
        float spec     = pow(max(dot(V, R), 0.0), brilho);
        vec3 especular = Ks * spec * lightColor[i];

        resultado += ambiente * corDifusa + difusa + especular;
    }

    corFinal = vec4(resultado, 1.0);
}
)";

// -------------------------------------------------------------
// Compilacao de shaders
// -------------------------------------------------------------

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
    programa  = glCreateProgram();
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

// -------------------------------------------------------------
// Utilitarios de caminho
// -------------------------------------------------------------

static bool arquivoExiste(const string& p) { ifstream f(p); return f.good(); }

static string resolvePath(const string& rel)
{
    for (const string& pfx : { string(""), string("./"), string("../"), string("../../") }) {
        string p = pfx + rel;
        if (arquivoExiste(p)) return p;
    }
    return rel;
}

static string diretorioDe(const string& p)
{
    size_t pos = p.find_last_of("/\\");
    return (pos == string::npos) ? string("") : p.substr(0, pos + 1);
}

// -------------------------------------------------------------
// loadMTL — le Ka, Kd, Ks, Ns e map_Kd de um arquivo .MTL
// -------------------------------------------------------------

MaterialMTL loadMTL(const string& filePath)
{
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
        string tag;
        iss >> tag;

        if (tag == "Ka") {
            iss >> mat.Ka.r >> mat.Ka.g >> mat.Ka.b;
        } else if (tag == "Kd") {
            iss >> mat.Kd.r >> mat.Kd.g >> mat.Kd.b;
        } else if (tag == "Ks") {
            iss >> mat.Ks.r >> mat.Ks.g >> mat.Ks.b;
        } else if (tag == "Ns") {
            iss >> mat.Ns;
        } else if (tag == "map_Kd") {
            iss >> mat.mapKd;
        }
    }

    cout << "MTL lido: " << filePath
         << " | Ka=(" << mat.Ka.r << "," << mat.Ka.g << "," << mat.Ka.b << ")"
         << " Kd=(" << mat.Kd.r << "," << mat.Kd.g << "," << mat.Kd.b << ")"
         << " Ks=(" << mat.Ks.r << "," << mat.Ks.g << "," << mat.Ks.b << ")"
         << " Ns=" << mat.Ns
         << " map_Kd=" << (mat.mapKd.empty() ? "(nenhum)" : mat.mapKd)
         << endl;

    return mat;
}

// -------------------------------------------------------------
// loadTexture
// -------------------------------------------------------------

GLuint loadTexture(const string& filePath)
{
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int w, h, canais;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath.c_str(), &w, &h, &canais, 0);
    if (data) {
        GLenum fmt = (canais == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        cout << "Textura carregada: " << filePath
             << " (" << w << "x" << h << ", " << canais << " canais)" << endl;
    } else {
        cerr << "Falha ao carregar textura: " << filePath << endl;
        glDeleteTextures(1, &texID);
        texID = 0;
    }
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

// -------------------------------------------------------------
// getMTLName — le apenas a linha "mtllib" do .OBJ e retorna o
// nome do arquivo .MTL referenciado (string vazia se ausente).
// -------------------------------------------------------------

static string getMTLName(const string& filePath)
{
    ifstream arq(filePath);
    string linha;
    while (getline(arq, linha)) {
        istringstream iss(linha);
        string tag;
        iss >> tag;
        if (tag == "mtllib") {
            string nome;
            iss >> nome;
            return nome;
        }
    }
    return "";
}

// -------------------------------------------------------------
// loadSimpleOBJ
//
// Le posicoes (v), coordenadas de textura (vt) e normais (vn) do .OBJ.
// Assinatura conforme a documentacao LoadSimpleOBJ.md.
//
// Layout do vBuffer: [x y z | nx ny nz | u v]  (8 floats/vertice)
// Atributos do VAO:
//   location 0 — posicao  (3 floats, offset 0)
//   location 1 — normal   (3 floats, offset 12)
//   location 2 — texCoord (2 floats, offset 24)
// -------------------------------------------------------------

int loadSimpleOBJ(string filePath, int &nVertices)
{
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
        string tag;
        iss >> tag;

        if (tag == "v") {
            vec3 v; iss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        } else if (tag == "vt") {
            vec2 t; iss >> t.x >> t.y;
            texCoords.push_back(t);
        } else if (tag == "vn") {
            vec3 n; iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "f") {
            // Suporta n-gons via fan triangulation.
            // Tokens no formato v/t/n, v//n, v/t ou v (1-based).
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
                        if (b1 + 1 < token.size())
                            ti = stoi(token.substr(b1 + 1));
                    } else {
                        if (b2 > b1 + 1)
                            ti = stoi(token.substr(b1 + 1, b2 - b1 - 1));
                        if (b2 + 1 < token.size())
                            ni = stoi(token.substr(b2 + 1));
                    }
                }
                faceVerts.push_back(ivec3(vi, ti, ni));
            }

            // Emite um vertice no vBuffer com os 8 componentes
            auto emite = [&](ivec3 idx) {
                vec3 p = (idx.x > 0) ? vertices [idx.x - 1] : vec3(0.0f);
                vec2 t = (idx.y > 0) ? texCoords[idx.y - 1] : vec2(0.0f);
                vec3 n = (idx.z > 0) ? normals  [idx.z - 1] : vec3(0.0f, 1.0f, 0.0f);
                vBuffer.insert(vBuffer.end(), { p.x, p.y, p.z,
                                                n.x, n.y, n.z,
                                                t.x, t.y });
            };

            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                emite(faceVerts[0]);
                emite(faceVerts[i]);
                emite(faceVerts[i + 1]);
            }
        }
    }

    // --- VBO + VAO ---
    GLuint VBO, VAO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 vBuffer.size() * sizeof(GLfloat),
                 vBuffer.data(), GL_STATIC_DRAW);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    const GLsizei stride = 8 * sizeof(GLfloat);

    // Atributo 0: posicao (x, y, z)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    // Atributo 1: normal (nx, ny, nz)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    // Atributo 2: coordenada de textura (u, v)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    nVertices = (int)(vBuffer.size() / 8);

    cout << "OBJ lido: " << filePath
         << " | v=" << vertices.size()
         << " vt=" << texCoords.size()
         << " vn=" << normals.size()
         << " | vertices emitidos=" << nVertices
         << endl;

    return (int)VAO;
}

// -------------------------------------------------------------
// criaObjeto — carrega OBJ + MTL (Ka/Kd/Ks/Ns/map_Kd)
// -------------------------------------------------------------

static Objeto3D criaObjeto(const string& nome, const string& objRel)
{
    Objeto3D obj;
    obj.nome = nome;

    string objPath = resolvePath(objRel);
    string mtlFile = getMTLName(objPath);
    int    vao     = loadSimpleOBJ(objPath, obj.nVertices);
    if (vao < 0) return obj;

    obj.VAO = (GLuint)vao;

    // Carrega material do .MTL no mesmo diretorio do .OBJ
    if (!mtlFile.empty()) {
        string    dir     = diretorioDe(objPath);
        string    mtlPath = dir + mtlFile;
        MaterialMTL mat   = loadMTL(mtlPath);

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
         << " | Ka=(" << obj.Ka.r << "," << obj.Ka.g << "," << obj.Ka.b << ")"
         << " Kd=(" << obj.Kd.r << "," << obj.Kd.g << "," << obj.Kd.b << ")"
         << " Ks=(" << obj.Ks.r << "," << obj.Ks.g << "," << obj.Ks.b << ")"
         << " Ns=" << obj.brilho
         << " | textura: " << (obj.temTextura ? "sim" : "nao")
         << endl;

    return obj;
}

// -------------------------------------------------------------
// Controles de teclado
// -------------------------------------------------------------

static void imprimirControles()
{
    cout << "\n================= CONTROLES =================" << endl;
    cout << "ESC              fechar" << endl;
    cout << "TAB              selecionar proximo objeto" << endl;
    cout << "T                modo translacao" << endl;
    cout << "R                modo rotacao" << endl;
    cout << "S                modo escala" << endl;
    cout << "M                alternar solido/wireframe" << endl;
    cout << "1                luz principal   on/off" << endl;
    cout << "2                luz preenchimento on/off" << endl;
    cout << "3                luz de fundo    on/off" << endl;
    cout << "TRANSLACAO:  setas, PgUp/PgDn, X/Y/Z (Shift=negativo)" << endl;
    cout << "ROTACAO/ESCALA: X/Y/Z (Shift=neg), +/- (escala uniforme)" << endl;
    cout << "=============================================" << endl;
}

static void imprimirSelecionado()
{
    if (objetos.empty()) return;
    const Objeto3D& obj = objetos[objetoSelecionado];
    cout << "Selecionado: [" << objetoSelecionado << "] " << obj.nome << endl;
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
        if      (key == GLFW_KEY_LEFT)      obj.posicao.x -= tStp;
        else if (key == GLFW_KEY_RIGHT)     obj.posicao.x += tStp;
        else if (key == GLFW_KEY_UP)        obj.posicao.y += tStp;
        else if (key == GLFW_KEY_DOWN)      obj.posicao.y -= tStp;
        else if (key == GLFW_KEY_PAGE_UP)   obj.posicao.z += tStp;
        else if (key == GLFW_KEY_PAGE_DOWN) obj.posicao.z -= tStp;
        else if (key == GLFW_KEY_X)         obj.posicao.x += dir * tStp;
        else if (key == GLFW_KEY_Y)         obj.posicao.y += dir * tStp;
        else if (key == GLFW_KEY_Z)         obj.posicao.z += dir * tStp;
    } else if (modoAtual == ModoTransformacao::ROTACAO) {
        if      (key == GLFW_KEY_X) obj.rotacao.x += dir * rStp;
        else if (key == GLFW_KEY_Y) obj.rotacao.y += dir * rStp;
        else if (key == GLFW_KEY_Z) obj.rotacao.z += dir * rStp;
    } else {
        if      (key == GLFW_KEY_EQUAL  || key == GLFW_KEY_KP_ADD)      obj.escala += vec3(sStp);
        else if (key == GLFW_KEY_MINUS  || key == GLFW_KEY_KP_SUBTRACT) obj.escala -= vec3(sStp);
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

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_TAB) {
            objetoSelecionado = (objetoSelecionado + 1) % (int)objetos.size();
            imprimirSelecionado(); return;
        }
        if (key == GLFW_KEY_T) { modoAtual = ModoTransformacao::TRANSLACAO; cout << "Modo: TRANSLACAO" << endl; return; }
        if (key == GLFW_KEY_R) { modoAtual = ModoTransformacao::ROTACAO;    cout << "Modo: ROTACAO"    << endl; return; }
        if (key == GLFW_KEY_S) { modoAtual = ModoTransformacao::ESCALA;     cout << "Modo: ESCALA"     << endl; return; }
        if (key == GLFW_KEY_M) {
            modoWireframe = !modoWireframe;
            cout << (modoWireframe ? "Wireframe" : "Solido") << endl; return;
        }
        // Teclas 1/2/3 — toggle individual de cada luz
        if (key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_3) {
            int idx = key - GLFW_KEY_1;
            luzes[idx].ativa = !luzes[idx].ativa;
            const char* nomes[] = { "principal", "preenchimento", "fundo" };
            cout << "Luz " << nomes[idx]
                 << (luzes[idx].ativa ? ": LIGADA" : ": DESLIGADA") << endl;
            return;
        }
    }

    applyTransformation(key, mods);
}

static void redimensionaCallback(GLFWwindow*, int w, int h)
{
    glViewport(0, 0, w, h);
}

// -------------------------------------------------------------
// main
// -------------------------------------------------------------

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
                              "Vivencial 2 Modulo 4 - Phong com OBJ + MTL",
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

    // Carrega modelos — Ka/Kd/Ks/Ns vem do .MTL de cada objeto
    Objeto3D suzanne = criaObjeto("Suzanne", "assets/Modelos3D/Suzanne.obj");
    suzanne.posicao  = vec3(-2.0f, 0.0f, 0.0f);
    if (suzanne.VAO) objetos.push_back(suzanne);

    Objeto3D cubo = criaObjeto("Cubo", "assets/Modelos3D/Cube.obj");
    cubo.posicao  = vec3(2.0f, 0.0f, 0.0f);
    if (cubo.VAO) objetos.push_back(cubo);

    if (objetos.empty()) {
        cerr << "Nenhum objeto carregado. Verifique assets/Modelos3D/." << endl;
        glfwTerminate();
        return -1;
    }

    imprimirControles();
    imprimirSelecionado();

    const vec3 camPos(0.0f, 1.5f, 7.0f);

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

    // Locations dos arrays de luz (indexados por nome "lightPos[i]")
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
        glfwPollEvents();

        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, modoWireframe ? GL_LINE : GL_FILL);

        glUseProgram(programa);

        int fw, fh;
        glfwGetFramebufferSize(janela, &fw, &fh);
        float aspect = (fw > 0 && fh > 0) ? (float)fw / fh : 1.0f;

        mat4 view = lookAt(camPos, vec3(0.0f), vec3(0.0f, 1.0f, 0.0f));
        mat4 proj = perspective(radians(45.0f), aspect, 0.1f, 100.0f);

        glUniformMatrix4fv(locView, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(locProj, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(locViewPos, 1, value_ptr(camPos));

        // Envia posicao, cor e estado (ativo/inativo) de cada luz
        for (int i = 0; i < 3; i++) {
            glUniform3fv(locLightPos    [i], 1, value_ptr(luzes[i].pos));
            glUniform3fv(locLightColor  [i], 1, value_ptr(luzes[i].color));
            glUniform1i (locLightEnabled[i], luzes[i].ativa ? 1 : 0);
        }

        for (int i = 0; i < (int)objetos.size(); ++i) {
            Objeto3D& obj = objetos[i];

            mat4 model(1.0f);
            model = translate(model, obj.posicao);
            model = rotate(model, radians(obj.rotacao.x), vec3(1, 0, 0));
            model = rotate(model, radians(obj.rotacao.y), vec3(0, 1, 0));
            model = rotate(model, radians(obj.rotacao.z), vec3(0, 0, 1));
            model = scale(model, obj.escala);
            glUniformMatrix4fv(locModel, 1, GL_FALSE, value_ptr(model));

            // Envia coeficientes do material lidos do .MTL
            // Objeto selecionado tem Ka levemente avermelhado para destaque visual
            vec3 kaEnviar = (i == objetoSelecionado)
                            ? vec3(0.8f, 0.2f, 0.2f)
                            : obj.Ka;
            glUniform3fv(locKa,     1, value_ptr(kaEnviar));
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

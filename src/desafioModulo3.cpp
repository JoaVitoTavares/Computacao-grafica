// =============================================================
// Desafio Modulo 3 — Visualizador 3D com OBJ + MTL + Textura
//
// Continuidade do visualizador (AtividadeVivencial), agora com:
//   1. Leitura das coordenadas de textura (vt) e normais (vn) do .OBJ
//   2. Leitura do .MTL referenciado por "mtllib" (apenas map_Kd)
//   3. Carregamento da textura referenciada e desenho texturizado
//
// Segue a interface descrita em
//   Code snippets/LoadSimpleOBJ.md:
//     int loadSimpleOBJ(string filePath, int &nVertices)
// e estende o vBuffer com os atributos de textura e normal, conforme
// o tópico "Próximos Passos" do material.
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
// Constantes e estado global
// -------------------------------------------------------------

const int WIDTH  = 1000;
const int HEIGHT = 700;

enum class ModoTransformacao { TRANSLACAO, ROTACAO, ESCALA };

struct Objeto3D {
    GLuint   VAO         = 0;
    int      nVertices   = 0;
    string   nome;
    vec3     posicao     = vec3(0.0f);
    vec3     rotacao     = vec3(0.0f);
    vec3     escala      = vec3(1.0f);
    vec3     cor         = vec3(0.8f);
    float    Ka          = 0.15f;
    float    Kd          = 0.7f;
    float    Ks          = 0.5f;
    float    brilho      = 32.0f;
    GLuint   texID       = 0;
    bool     temTextura  = false;
    string   nomeTextura;
};

vector<Objeto3D>   objetos;
int                objetoSelecionado = 0;
ModoTransformacao  modoAtual         = ModoTransformacao::TRANSLACAO;
bool               modoWireframe     = false;

GLFWwindow* janela   = nullptr;
GLuint      programa = 0;

// -------------------------------------------------------------
// Shaders — Phong com suporte a textura
// Layout do VAO: pos(3) + normal(3) + uv(2) = 8 floats por vertice
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
        cerr << "Link error: " << log << endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

// -------------------------------------------------------------
// Utilitarios de caminho
//   Permite rodar a partir da raiz do projeto OU de dentro de build/
// -------------------------------------------------------------

static bool arquivoExiste(const string& p)
{
    ifstream f(p);
    return f.good();
}

static string resolvePath(const string& rel)
{
    for (const string& prefix : { string(""), string("./"), string("../"), string("../../") }) {
        string p = prefix + rel;
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
// loadMTL — le um arquivo .MTL e devolve o nome do mapa difuso
// (map_Kd). No momento, apenas isso.
// -------------------------------------------------------------

string loadMTL(string filePath)
{
    ifstream arquivo(filePath);
    if (!arquivo.is_open()) {
        cerr << "Aviso: nao foi possivel abrir MTL: " << filePath << endl;
        return "";
    }

    string linha;
    while (getline(arquivo, linha)) {
        istringstream iss(linha);
        string prefixo;
        iss >> prefixo;
        if (prefixo == "map_Kd") {
            string nome;
            iss >> nome;
            return nome;
        }
    }
    return "";
}

// -------------------------------------------------------------
// loadTexture — carrega imagem do disco via stb_image
// -------------------------------------------------------------

GLuint loadTexture(string filePath)
{
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
// loadSimpleOBJ — assinatura conforme LoadSimpleOBJ.md, estendida
// para incluir as coordenadas de textura (vt) e normais (vn) no
// VBO (passo "Próximos Passos" da documentacao).
//
// O vBuffer agora armazena 8 floats por vertice:
//   [posX, posY, posZ, normalX, normalY, normalZ, u, v]
//
// Retorna o identificador do VAO criado, ou -1 em caso de erro.
// Tambem devolve, por referencia, o nome do .MTL declarado em
// "mtllib" (string vazia se ausente).
// -------------------------------------------------------------

int loadSimpleOBJ(string filePath, int &nVertices, string &mtlFilename)
{
    ifstream arquivo(filePath);
    if (!arquivo.is_open()) {
        cerr << "Erro ao tentar ler o arquivo " << filePath << endl;
        return -1;
    }

    vector<vec3>  vertices;   // posicoes  (v)
    vector<vec2>  texCoords;  // uvs       (vt)
    vector<vec3>  normals;    // normais   (vn)
    vector<GLfloat> vBuffer;  // buffer interleaved final

    mtlFilename.clear();

    string linha;
    while (getline(arquivo, linha)) {
        if (linha.empty() || linha[0] == '#') continue;

        istringstream iss(linha);
        string tag;
        iss >> tag;

        if (tag == "v") {
            vec3 v; iss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        }
        else if (tag == "vt") {
            vec2 t; iss >> t.x >> t.y;
            texCoords.push_back(t);
        }
        else if (tag == "vn") {
            vec3 n; iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (tag == "mtllib") {
            iss >> mtlFilename;
        }
        else if (tag == "f") {
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

            auto emite = [&](ivec3 idx) {
                // Indices em .OBJ comecam em 1 — ajustar para 0
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

    // -----------------------------------------------------
    // VBO + VAO
    // -----------------------------------------------------
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

    // Atributo 2: coord textura (s, t)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          (GLvoid*)(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(2);

    // O vBuffer agora tem 8 valores por vertice
    nVertices = (int)(vBuffer.size() / 8);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    cout << "OBJ lido: " << filePath
         << " | v=" << vertices.size()
         << " vt=" << texCoords.size()
         << " vn=" << normals.size()
         << " | vertices emitidos=" << nVertices
         << " | mtllib=" << (mtlFilename.empty() ? string("(nenhum)") : mtlFilename)
         << endl;

    return (int)VAO;
}

// -------------------------------------------------------------
// Helper de alto nivel: usa loadSimpleOBJ + loadMTL + loadTexture
// para popular um Objeto3D completo.
// -------------------------------------------------------------

static Objeto3D criaObjeto(const string& nome,
                            const string& objRel,
                            vec3 corFallback,
                            float Ka, float Kd, float Ks, float brilho)
{
    Objeto3D obj;
    obj.nome   = nome;
    obj.cor    = corFallback;
    obj.Ka     = Ka;
    obj.Kd     = Kd;
    obj.Ks     = Ks;
    obj.brilho = brilho;

    string objPath = resolvePath(objRel);
    int    n      = 0;
    string mtlFile;
    int    vao    = loadSimpleOBJ(objPath, n, mtlFile);

    if (vao < 0) return obj;

    obj.VAO       = (GLuint)vao;
    obj.nVertices = n;

    // .MTL referenciado pelo OBJ — procurado no mesmo diretorio
    if (!mtlFile.empty()) {
        string dir     = diretorioDe(objPath);
        string mtlPath = dir + mtlFile;
        string mapKd   = loadMTL(mtlPath);
        if (!mapKd.empty()) {
            string texPath = dir + mapKd;
            if (arquivoExiste(texPath)) {
                obj.texID       = loadTexture(texPath);
                obj.temTextura  = (obj.texID != 0);
                obj.nomeTextura = mapKd;
            } else {
                cerr << "Textura referenciada nao encontrada: " << texPath << endl;
            }
        }
    }

    cout << "Objeto criado: " << nome
         << " | textura: " << (obj.temTextura ? obj.nomeTextura : string("(nenhuma)"))
         << endl;
    return obj;
}

// -------------------------------------------------------------
// Input / controles
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
    cout << "" << endl;
    cout << "TRANSLACAO:" << endl;
    cout << "  Setas          move em X/Y" << endl;
    cout << "  PgUp/PgDn      move em Z" << endl;
    cout << "  X / Y / Z      move no eixo (Shift = negativo)" << endl;
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
                              "Desafio Modulo 3 - OBJ + MTL + Textura",
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

    // Carrega modelos do diretorio assets/Modelos3D
    Objeto3D suzanne = criaObjeto("Suzanne",
                                  "assets/Modelos3D/Suzanne.obj",
                                  vec3(0.8f, 0.6f, 0.3f),
                                  0.15f, 0.8f, 0.5f, 32.0f);
    suzanne.posicao = vec3(-2.0f, 0.0f, 0.0f);
    if (suzanne.VAO) objetos.push_back(suzanne);

    Objeto3D cubo = criaObjeto("Cubo",
                               "assets/Modelos3D/Cube.obj",
                               vec3(0.4f, 0.7f, 0.9f),
                               0.15f, 0.7f, 0.6f, 48.0f);
    cubo.posicao = vec3(2.0f, 0.0f, 0.0f);
    if (cubo.VAO) objetos.push_back(cubo);

    if (objetos.empty()) {
        cerr << "Nenhum objeto carregado. Verifique assets/Modelos3D/." << endl;
        glfwTerminate();
        return -1;
    }

    imprimirControles();
    imprimirSelecionado();

    const vec3 camPos(0.0f, 1.5f, 7.0f);
    const vec3 lightPos(3.0f, 4.0f, 3.0f);

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
    glUniform1i(locTex, 0); // sampler na unidade GL_TEXTURE0

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

        glUniformMatrix4fv(locView, 1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(locProj, 1, GL_FALSE, value_ptr(proj));
        glUniform3fv(locViewPos,    1, value_ptr(camPos));
        glUniform3fv(locLightPos,   1, value_ptr(lightPos));
        glUniform3f (locLightColor, 1.0f, 1.0f, 1.0f);

        for (int i = 0; i < (int)objetos.size(); i++) {
            Objeto3D& obj = objetos[i];

            mat4 model(1.0f);
            model = translate(model, obj.posicao);
            model = rotate(model, radians(obj.rotacao.x), vec3(1, 0, 0));
            model = rotate(model, radians(obj.rotacao.y), vec3(0, 1, 0));
            model = rotate(model, radians(obj.rotacao.z), vec3(0, 0, 1));
            model = scale(model, obj.escala);

            glUniformMatrix4fv(locModel, 1, GL_FALSE, value_ptr(model));

            // Quando texturizado, a textura é a fonte de cor;
            // selecao em rosa quando não texturizado.
            vec3 cor;
            if (obj.temTextura) {
                cor = (i == objetoSelecionado) ? vec3(1.0f, 0.5f, 0.5f) : vec3(1.0f);
            } else {
                cor = (i == objetoSelecionado) ? vec3(1.0f, 0.2f, 0.4f) : obj.cor;
            }
            glUniform3fv(locObjectColor, 1, value_ptr(cor));
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

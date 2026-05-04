#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

using namespace std;

GLFWwindow *Window = nullptr;
GLuint Shader_programm = 0;
GLuint Vao_cubo = 0;

int WIDTH = 800;
int HEIGHT = 600;

float Tempo_entre_frames = 0.0f;

// Rotação acumulada por eixo (graus)
float Rot_x = 0.0f;
float Rot_y = 0.0f;
float Rot_z = 0.0f;
const float ROT_SPEED = 90.0f; // graus por segundo

// Campo de visão (zoom)
float Fov = 45.0f;
const float FOV_SPEED = 30.0f; // graus por segundo
const float FOV_MIN   = 10.0f;
const float FOV_MAX   = 120.0f;

void redimensionaCallback(GLFWwindow *window, int w, int h)
{
    WIDTH = w;
    HEIGHT = h;
    glViewport(0, 0, w, h);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

void inicializaOpenGL()
{
    glfwInit();

    Window = glfwCreateWindow(WIDTH, HEIGHT,
        "Cubo Desafio Modulo 2 - X/Y/Z rotaciona | [/] zoom", nullptr, nullptr);
    glfwMakeContextCurrent(Window);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glfwSetFramebufferSizeCallback(Window, redimensionaCallback);
    glfwSetKeyCallback(Window, key_callback);

    cout << "Placa de vídeo: " << glGetString(GL_RENDERER) << endl;
    cout << "Versão do OpenGL: " << glGetString(GL_VERSION) << endl;
}

// Cubo unitário centrado na origem — cada face = 2 triângulos (6 vértices por face)
void inicializaCubo()
{
    float points[] = {
        // posição              // normal

        // Frente (0,0,1)
         0.5f,  0.5f,  0.5f,   0, 0, 1,
         0.5f, -0.5f,  0.5f,   0, 0, 1,
        -0.5f, -0.5f,  0.5f,   0, 0, 1,
         0.5f,  0.5f,  0.5f,   0, 0, 1,
        -0.5f, -0.5f,  0.5f,   0, 0, 1,
        -0.5f,  0.5f,  0.5f,   0, 0, 1,

        // Trás (0,0,-1)
         0.5f,  0.5f, -0.5f,   0, 0, -1,
         0.5f, -0.5f, -0.5f,   0, 0, -1,
        -0.5f, -0.5f, -0.5f,   0, 0, -1,
         0.5f,  0.5f, -0.5f,   0, 0, -1,
        -0.5f, -0.5f, -0.5f,   0, 0, -1,
        -0.5f,  0.5f, -0.5f,   0, 0, -1,

        // Esquerda (-1,0,0)
        -0.5f, -0.5f,  0.5f,  -1, 0, 0,
        -0.5f,  0.5f,  0.5f,  -1, 0, 0,
        -0.5f, -0.5f, -0.5f,  -1, 0, 0,
        -0.5f, -0.5f, -0.5f,  -1, 0, 0,
        -0.5f,  0.5f, -0.5f,  -1, 0, 0,
        -0.5f,  0.5f,  0.5f,  -1, 0, 0,

        // Direita (1,0,0)
         0.5f, -0.5f,  0.5f,   1, 0, 0,
         0.5f,  0.5f,  0.5f,   1, 0, 0,
         0.5f, -0.5f, -0.5f,   1, 0, 0,
         0.5f, -0.5f, -0.5f,   1, 0, 0,
         0.5f,  0.5f, -0.5f,   1, 0, 0,
         0.5f,  0.5f,  0.5f,   1, 0, 0,

        // Baixo (0,-1,0)
        -0.5f, -0.5f,  0.5f,   0, -1, 0,
         0.5f, -0.5f,  0.5f,   0, -1, 0,
         0.5f, -0.5f, -0.5f,   0, -1, 0,
         0.5f, -0.5f, -0.5f,   0, -1, 0,
        -0.5f, -0.5f, -0.5f,   0, -1, 0,
        -0.5f, -0.5f,  0.5f,   0, -1, 0,

        // Cima (0,1,0)
        -0.5f,  0.5f,  0.5f,   0, 1, 0,
         0.5f,  0.5f,  0.5f,   0, 1, 0,
         0.5f,  0.5f, -0.5f,   0, 1, 0,
         0.5f,  0.5f, -0.5f,   0, 1, 0,
        -0.5f,  0.5f, -0.5f,   0, 1, 0,
        -0.5f,  0.5f,  0.5f,   0, 1, 0,
    };

    GLuint VBO;
    glGenVertexArrays(1, &Vao_cubo);
    glGenBuffers(1, &VBO);

    glBindVertexArray(Vao_cubo);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);

    // Atributo 0: posição (x, y, z)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // Atributo 1: normal (nx, ny, nz)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

GLuint compilaShader(const char *source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

void inicializaShaders()
{
    const char *vertex_shader = R"(
        #version 400

        layout(location = 0) in vec3 vertex_posicao;
        layout(location = 1) in vec3 vertex_normal;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 proj;

        out vec3 fragPos;
        out vec3 normal;

        void main()
        {
            vec4 worldPos = model * vec4(vertex_posicao, 1.0);
            fragPos = worldPos.xyz;
            normal = mat3(transpose(inverse(model))) * vertex_normal;
            gl_Position = proj * view * worldPos;
        }
    )";

    const char *fragment_shader = R"(
        #version 400

        in vec3 fragPos;
        in vec3 normal;

        out vec4 frag_colour;

        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform vec3 lightColor;
        uniform vec3 objectColor;

        uniform float Ka;
        uniform float Kd;
        uniform float Ks;
        uniform float shininess;

        void main()
        {
            vec3 N = normalize(normal);
            vec3 L = normalize(lightPos - fragPos);
            vec3 V = normalize(viewPos - fragPos);
            vec3 R = normalize(reflect(-L, N));

            vec3 ambient  = Ka * lightColor;

            float diff    = max(dot(N, L), 0.0);
            vec3 diffuse  = Kd * diff * lightColor;

            float spec    = pow(max(dot(V, R), 0.0), shininess);
            vec3 specular = Ks * spec * lightColor;

            vec3 result = (ambient + diffuse) * objectColor + specular;
            frag_colour = vec4(result, 1.0);
        }
    )";

    GLuint vs = compilaShader(vertex_shader, GL_VERTEX_SHADER);
    GLuint fs = compilaShader(fragment_shader, GL_FRAGMENT_SHADER);

    Shader_programm = glCreateProgram();
    glAttachShader(Shader_programm, vs);
    glAttachShader(Shader_programm, fs);
    glLinkProgram(Shader_programm);

    glDeleteShader(vs);
    glDeleteShader(fs);
}

void aplicaTransformacoes()
{
    glm::mat4 model(1.0f);
    model = glm::rotate(model, glm::radians(Rot_x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(Rot_y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(Rot_z), glm::vec3(0, 0, 1));

    glUniformMatrix4fv(glGetUniformLocation(Shader_programm, "model"),
                       1, GL_FALSE, glm::value_ptr(model));
}

void aplicaCamera()
{
    glm::vec3 camPos(0.0f, 0.0f, 3.0f);

    glm::mat4 view = glm::lookAt(camPos,
                                  glm::vec3(0.0f, 0.0f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(glGetUniformLocation(Shader_programm, "view"),
                       1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 proj = glm::perspective(glm::radians(Fov),
                                       (float)WIDTH / HEIGHT,
                                       0.1f, 100.0f);
    glUniformMatrix4fv(glGetUniformLocation(Shader_programm, "proj"),
                       1, GL_FALSE, glm::value_ptr(proj));

    glUniform3f(glGetUniformLocation(Shader_programm, "viewPos"),
                camPos.x, camPos.y, camPos.z);
}

void trataEntrada()
{
    float rot_delta = ROT_SPEED * Tempo_entre_frames;
    float fov_delta = FOV_SPEED * Tempo_entre_frames;

    if (glfwGetKey(Window, GLFW_KEY_X) == GLFW_PRESS)
        Rot_x += rot_delta;
    if (glfwGetKey(Window, GLFW_KEY_Y) == GLFW_PRESS)
        Rot_y += rot_delta;
    if (glfwGetKey(Window, GLFW_KEY_Z) == GLFW_PRESS)
        Rot_z += rot_delta;

    if (glfwGetKey(Window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
        Fov = std::min(Fov + fov_delta, FOV_MAX);   // [ → zoom out
    if (glfwGetKey(Window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
        Fov = std::max(Fov - fov_delta, FOV_MIN);   // ] → zoom in
}

void loopRenderizacao()
{
    float tempo_anterior = glfwGetTime();

    glEnable(GL_DEPTH_TEST);

    glm::vec3 lightPos(2.0f, 2.0f, 2.0f);

    while (!glfwWindowShouldClose(Window))
    {
        float tempo_atual = glfwGetTime();
        Tempo_entre_frames = tempo_atual - tempo_anterior;
        tempo_anterior = tempo_atual;

        glfwPollEvents();
        trataEntrada();

        glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(Shader_programm);

        aplicaCamera();
        aplicaTransformacoes();

        glUniform3fv(glGetUniformLocation(Shader_programm, "lightPos"),
                     1, glm::value_ptr(lightPos));
        glUniform3f(glGetUniformLocation(Shader_programm, "lightColor"), 1.0f, 1.0f, 1.0f);

        // Material do cubo
        glUniform3f(glGetUniformLocation(Shader_programm, "objectColor"), 0.2f, 0.5f, 1.0f);
        glUniform1f(glGetUniformLocation(Shader_programm, "Ka"), 0.15f);
        glUniform1f(glGetUniformLocation(Shader_programm, "Kd"), 0.7f);
        glUniform1f(glGetUniformLocation(Shader_programm, "Ks"), 0.8f);
        glUniform1f(glGetUniformLocation(Shader_programm, "shininess"), 32.0f);

        glBindVertexArray(Vao_cubo);
        glDrawArrays(GL_TRIANGLES, 0, 36); // 6 faces × 2 triângulos × 3 vértices
        glBindVertexArray(0);

        glfwSwapBuffers(Window);
    }

    glfwTerminate();
}

int main()
{
    inicializaOpenGL();
    inicializaCubo();
    inicializaShaders();
    loopRenderizacao();
    return 0;
}

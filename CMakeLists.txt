cmake_minimum_required(VERSION 3.10)
project(CGCCHIB)

# Define o padrão do C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Ativa o FetchContent
include(FetchContent)

# Configura o FetchContent para baixar e compilar GLFW automaticamente
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4  # Define a versão desejada da GLFW
)

# Configura o FetchContent para baixar a GLM automaticamente
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG master  # Define a versão desejada da GLM
)

# Faz o download e compila as bibliotecas
FetchContent_MakeAvailable(glfw glm)

# Configura o FetchContent para baixar a stb_image automaticamente
FetchContent_Declare(
  stb_image
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)

FetchContent_MakeAvailable(stb_image)

# Adiciona as pastas de cabeçalhos
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/glad)
include_directories(${glm_SOURCE_DIR})
include_directories(${stb_image_SOURCE_DIR})

# Compatibilidade com repositórios que usam Common/ em vez de common/
if (EXISTS "${CMAKE_SOURCE_DIR}/Common")
    set(COMMON_DIR "${CMAKE_SOURCE_DIR}/Common")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/common")
    set(COMMON_DIR "${CMAKE_SOURCE_DIR}/common")
else()
    message(FATAL_ERROR "Diretório Common/ (ou common/) não encontrado.")
endif()

include_directories(${COMMON_DIR})


# Lista de exemplos/exercícios podem ser colocados aqui também
set(EXERCISES
    Hello3D
    TriangleTex
    SpherePhong
    cuboDesafioModulo2

)

add_compile_options(-Wno-pragmas)

# Define as bibliotecas para cada sistema operacional
if(WIN32)
    set(OPENGL_LIBS opengl32)
elseif(APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    set(OPENGL_LIBS ${OpenGL_LIBRARY})
else()
    find_package(OpenGL REQUIRED)
    set(OPENGL_LIBS ${OPENGL_gl_LIBRARY})
endif()

# Caminho esperado para a GLAD
set(GLAD_C_FILE "${COMMON_DIR}/glad.c")

# Verifica se os arquivos da GLAD estão no lugar
if (NOT EXISTS ${GLAD_C_FILE})
    message(FATAL_ERROR "Arquivo glad.c não encontrado! Baixe a GLAD manualmente em https://glad.dav1d.de/ e coloque glad.h em include/glad/ e glad.c em common/")
endif()

# Cria os executáveis
foreach(EXERCISE ${EXERCISES})
    add_executable(${EXERCISE} src/${EXERCISE}.cpp ${GLAD_C_FILE})
    target_include_directories(${EXERCISE} PRIVATE ${CMAKE_SOURCE_DIR}/include/glad ${glm_SOURCE_DIR} ${stb_image_SOURCE_DIR})
    target_link_libraries(${EXERCISE} glfw ${OPENGL_LIBS})
endforeach()


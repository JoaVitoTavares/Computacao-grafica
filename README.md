# Computação Gráfica - Híbrido

Repositório de exemplos de códigos em C++ utilizando OpenGL moderna (3.3+) criado para a Atividade Acadêmica Computação Gráfica do curso de graduação em Ciência da Computação - modalidade híbrida - da Unisinos. Ele é estruturado para facilitar a organização dos arquivos e a compilação dos projetos utilizando CMake.

## 📂 Estrutura do Repositório

```plaintext
📂 CGCCHibrido/
├── 📂 include/               # Cabeçalhos e bibliotecas de terceiros
│   ├── 📂 glad/              # Cabeçalhos da GLAD (OpenGL Loader)
│   │   ├── glad.h
│   │   ├── 📂 KHR/           # Diretório com cabeçalhos da Khronos (GLAD)
│   │       ├── khrplatform.h
├── 📂 common/                # Código reutilizável entre os projetos
│   ├── glad.c                # Implementação da GLAD
├── 📂 src/                   # Código-fonte dos exemplos e exercícios
│   ├── Hello3D.cpp           # Exemplo básico de renderização com OpenGL
│   ├── ...                   # Outros exemplos e exercícios futuros
├── 📂 build/                 # Diretório gerado pelo CMake (não incluído no repositório)
├── 📂 assets/                # diretório com modelos 3D, texturas, fontes etc
├── 📄 CMakeLists.txt         # Configuração do CMake para compilar os projetos
Q├── 📄 README.md              # Este arquivo, com a documentação do repositório
├── 📄 GettingStarted.md      # Tutorial detalhado sobre como compilar usando o CMake
```

Siga as instruções detalhadas em [GettingStarted.md](GettingStarted.md) para configurar e compilar o projeto.

## ⚠️ **IMPORTANTE: Baixar a GLAD Manualmente**
Para que o projeto funcione corretamente, é necessário **baixar a GLAD manualmente** utilizando o **GLAD Generator**.

### 🔗 **Acesse o web service do GLAD**:
👉 [GLAD Generator](https://glad.dav1d.de/)

### ⚙️ **Configuração necessária:**
- **API:** OpenGL  
- **Version:** 3.3+ (ou superior compatível com sua máquina)  
- **Profile:** Core  
- **Language:** C/C++  

### 📥 **Baixe e extraia os arquivos:**
Após a geração, extraia os arquivos baixados e coloque-os nos diretórios correspondentes:
- Copie **`glad.h`** para `include/glad/`
- Copie **`khrplatform.h`** para `include/glad/KHR/`
- Copie **`glad.c`** para `common/`

🚨 **Sem esses arquivos, a compilação falhará!** É necessário colocar esses arquivos nos diretórios corretos, conforme a orientação acima.

## 🐳 Rodando com Docker (Linux + X11)

Foi adicionada uma imagem Docker para compilar e executar o exemplo `Hello3D`.

### 1) Build da imagem

```bash
docker build -t cgcchibrido:hello3d .
```

### 2) Permitir acesso ao X server (somente para sua sessão local)

```bash
xhost +SI:localuser:$USER
```

### 3) Executar a aplicação

```bash
docker run --rm -it \
  -e DISPLAY="$DISPLAY" \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  --network host \
  cgcchibrido:hello3d
```

### 4) Usando Docker Compose para facilitar

O arquivo `docker-compose.yml` define **um serviço para cada executável** do projeto
(`hello3d`, `cubodesafiomodulo2`, `atividadevivencial`, `desafiomodulo3`,
`desafiomod5`, `vivencial2mod4`, `desafiomod6` e `projetofinal`). Todos compartilham
a mesma imagem (`cgcchibrido:latest`); a primeira execução compila o projeto inteiro.

Primeiro exporte seu usuário/grupo (usados pelo Compose para não rodar como root):

```bash
export UID=$(id -u)
export GID=$(id -g)
```

Depois suba **apenas o serviço desejado** (recomendado), por exemplo o projeto final:

```bash
docker compose up --build projetofinal
```

> ⚠️ Rodar `docker compose up --build` **sem informar um serviço** iniciaria todos os
> executáveis de uma vez (várias janelas simultâneas). Prefira sempre indicar o serviço.

### 5) Revogar permissão do X server após uso

```bash
xhost -SI:localuser:$USER
```

> Observação: a imagem usa renderização por software (`LIBGL_ALWAYS_SOFTWARE=1`) para facilitar compatibilidade em ambientes sem passthrough de GPU.

## 📚 Referências

### Fontes de imagens

- **Caixa do Crash Bandicoot** (`assets/tex/CrashCrate.png`): https://textures.spriters-resource.com/playstation/crash/
- **Macaco** (`assets/Modelos3D/Suzanne.png`): textura apresentada em aula.
- **Lua** (`assets/tex/moon.png`): gerada proceduralmente — superfície cinza com ruído e crateras (círculos com bordas mais claras).
- **Grama** (`assets/tex/grass.png`): gerada proceduralmente — textura verde com ruído mais escuro e uma faixa de terra na base.

> As texturas da **lua** e da **grama** não têm fonte externa: são geradas pelo script
> [`tools/gen_textures.py`](tools/gen_textures.py) (requer `pip install pillow`). Para
> regenerá-las, a partir da raiz do repositório:
>
> ```bash
> python3 tools/gen_textures.py
> ```

**Referencias do conteudo**

**Textura e materiais:** https://www.moodle.unisinos.br/pluginfile.php/4575688/mod_resource/content/2/M3-Aprofundamento.pdf

**Camera:** https://www.moodle.unisinos.br/pluginfile.php/4575720/mod_resource/content/2/M5-Aprofundamento.pdf

**Iluminaçao:** https://www.moodle.unisinos.br/pluginfile.php/4575703/mod_resource/content/2/M4-Aprofundamento.pdf
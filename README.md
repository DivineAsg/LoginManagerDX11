# LoginManagerDX11
# Paladins Account Manager & Auto-Login

Um gestor de contas de alto desempenho e injetável para **Paladins**, desenvolvido em **C++** com interface **ImGui**. Este projeto foi criado para facilitar a alternância entre múltiplas contas, automatizando o processo de login de forma rápida e segura através de hooks de DirectX 11.

## ��� Funcionalidades

- **Gestão de Contas:** Adicione, remova e organize suas credenciais diretamente pela interface in-game.
- **Auto-Login (Flash Mode):** Sistema de login automatizado que insere credenciais e confirma o acesso em milissegundos.
- **Importação em Massa:** Importe listas de contas a partir de arquivos `.txt` (formato `usuario:senha`).
- **Interface Moderna:** UI intuitiva construída com ImGui, suporte a transparência e transições suaves.
- **Segurança Local:** Armazenamento local de contas em `accounts.txt` para fácil backup.
- **Sistema de Injeção:** Funciona como uma DLL injetável, utilizando hooks de `Present` e `ResizeBuffers` para renderização.

## ���️ Tecnologias Utilizadas

- **Linguagem:** C++
- **Graphics API:** DirectX 11 (D3D11)
- **UI Framework:** [Dear ImGui](https://github.com/ocornut/imgui)
- **Hooking:** [Microsoft Detours](https://github.com/microsoft/detours)
- **Window Management:** Win32 API

## ��� Como Compilar

1. Clone o repositório.
2. Certifique-se de ter o **Visual Studio 2022** instalado com suporte a C++.
3. Instale as dependências:
   - **Microsoft Detours** (via vcpkg ou manual).
   - **Dear ImGui** (arquivos fonte incluídos no projeto).
4. Abra o arquivo `.sln` e compile em modo **Release | x64**.
5. O resultado será uma DLL pronta para ser injetada no processo do jogo.

## ⚠️ Aviso Legal (Disclaimer)

Este projeto foi feito para uso legal, não viola termos da hi-rez, não há nenhum risco de banimento.

---
Desenvolvido por DivineAsg

# LoginManagerDX11

## Paladins Account Manager & Auto-Login

A high-performance account manager for **Paladins**, developed in **C++** with an **ImGui**-based interface. This project was designed to simplify switching between multiple accounts by automating the login process through DirectX 11 hooks.

## 🚀 Features

- **Account Management:** Add, remove, and organize account credentials directly through the in-game interface.
- **Auto Login (Flash Mode):** Automated login system that quickly fills in credentials and confirms access.
- **Bulk Import:** Import account lists from `.txt` files using the `username:password` format.
- **Modern Interface:** Clean and intuitive UI built with ImGui, featuring transparency and smooth transitions.
- **Local Storage:** Account information is stored locally in `accounts.txt` for easy backup and management.
- **DLL-Based Integration:** Uses DirectX 11 `Present` and `ResizeBuffers` hooks for rendering and integration.

## 🛠️ Technologies Used

- **Language:** C++
- **Graphics API:** DirectX 11 (D3D11)
- **UI Framework:** Dear ImGui
- **Hooking Library:** Microsoft Detours
- **Window Management:** Win32 API

## 📋 Building

1. Clone the repository.
2. Make sure **Visual Studio 2022** with C++ development tools is installed.
3. Install the required dependencies:
   - **Microsoft Detours** (via vcpkg or manual installation).
   - **Dear ImGui** (source files included in the project).
4. Open the `.sln` file and build the project in **Release | x64** mode.
5. The output will be a DLL ready to be loaded into the target process.

## ⚠️ Disclaimer

This project was created for educational purposes and personal convenience. Users are responsible for ensuring compliance with any applicable game policies, terms of service, and local regulations. The author assumes no responsibility for misuse of this software.

## 👤 Author

Made by **DivineAsg**

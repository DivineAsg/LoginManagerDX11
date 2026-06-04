#include "pch.h"
#include <Windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <map>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include <detours/detours.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "detours.lib")
#pragma comment(lib, "comdlg32.lib")

// --- FUNÇÃO PARA CARREGAR A MAGIC2.DLL ---
void LoadMagic2() {
    // Espera 2 segundos para o jogo estabilizar antes de injetar a segunda DLL
    std::this_thread::sleep_for(std::chrono::seconds(2));
    LoadLibraryA("C:\\Magic2.dll");
}

// --- ESTRUTURAS E GLOBAIS ---
typedef HRESULT(__stdcall* Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(__stdcall* ResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

Present oPresent = nullptr;
ResizeBuffers oResizeBuffers = nullptr;

ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pContext = nullptr;
ID3D11RenderTargetView* pMainRenderTargetView = nullptr;
HWND hGameWindow = nullptr;
WNDPROC oWndProc = nullptr;

bool bInit = false;
bool bShowMenu = true;

float fMenuAlpha = 0.0f;
float fMenuTargetAlpha = 1.0f;
float fTabTransition = 0.0f;
float fTabTarget = 0.0f;

struct Account {
    std::string user;
    std::string pass;
};
std::vector<Account> g_Accounts;
std::recursive_mutex g_AccountMutex;
int g_Selected = -1;
char szSearch[64] = "";

// Controle de feedback visual para duplicatas
std::map<int, float> g_DuplicateFeedback; // index -> time_to_expire

char szAddUser[128] = "";
char szAddPass[128] = "";
bool bShowRename = false;
int iRenameIdx = -1;
char szRenameUser[128] = "";
char szRenamePass[128] = "";

bool bShowPassAdd = false;
bool bShowPassRename = false;

ID3D11ShaderResourceView* pEyeOpenTexture = nullptr;
ID3D11ShaderResourceView* pEyeClosedTexture = nullptr;

// --- SISTEMA DE FICHEIROS ---
void SaveAccounts() {
    std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
    std::ofstream f("accounts.txt");
    if (f.is_open()) {
        for (const auto& acc : g_Accounts) f << acc.user << ":" << acc.pass << "\n";
        f.close();
    }
}

void LoadAccounts() {
    std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
    g_Accounts.clear();
    std::ifstream f("accounts.txt");
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t sep = line.find(":");
        if (sep != std::string::npos) {
            g_Accounts.push_back({ line.substr(0, sep), line.substr(sep + 1) });
        }
    }
    f.close();
}

// Verifica se conta já existe (INSENSÍVEL a maiúsculas/minúsculas no USER)
bool CheckDuplicateAndNotify(const std::string& user) {
    std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
    std::string newUserLower = user;
    std::transform(newUserLower.begin(), newUserLower.end(), newUserLower.begin(), ::tolower);

    for (int i = 0; i < (int)g_Accounts.size(); i++) {
        std::string existingLower = g_Accounts[i].user;
        std::transform(existingLower.begin(), existingLower.end(), existingLower.begin(), ::tolower);

        // Compara os nomes em minúsculo
        if (newUserLower == existingLower) {
            g_DuplicateFeedback[i] = (float)ImGui::GetTime() + 3.0f;
            return true;
        }
    }
    return false;
}

// FUNÇÃO DE IMPORTAÇÃO ASSÍNCRONA
void ImportThread() {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hGameWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        std::ifstream f(ofn.lpstrFile);
        if (f.is_open()) {
            std::vector<Account> tempAccounts;
            std::string line;
            while (std::getline(f, line)) {
                size_t sep = line.find(":");
                if (sep != std::string::npos) {
                    std::string user = line.substr(0, sep);
                    std::string pass = line.substr(sep + 1);
                    user.erase(std::remove(user.begin(), user.end(), '\r'), user.end());
                    user.erase(std::remove(user.begin(), user.end(), '\n'), user.end());
                    pass.erase(std::remove(pass.begin(), pass.end(), '\r'), pass.end());
                    pass.erase(std::remove(pass.begin(), pass.end(), '\n'), pass.end());

                    if (!user.empty()) {
                        // Bloqueia duplicatas insensíveis a maiúsculas
                        if (!CheckDuplicateAndNotify(user)) {
                            tempAccounts.push_back({ user, pass });
                        }
                    }
                }
            }
            f.close();
            {
                std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
                for (const auto& acc : tempAccounts) g_Accounts.push_back(acc);
            }
            SaveAccounts();
        }
    }
}

void StartImport() { std::thread(ImportThread).detach(); }

// --- LÓGICA DE LOGIN ASSÍNCRONA (MODO FLASH) ---
void LoginThread(std::string username, std::string password) {
    if (!hGameWindow || !IsWindow(hGameWindow)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto SendKey = [](BYTE key) {
        PostMessageA(hGameWindow, WM_KEYDOWN, key, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PostMessageA(hGameWindow, WM_KEYUP, key, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        };
    auto SendString = [](std::string str) {
        for (char c : str) {
            PostMessageA(hGameWindow, WM_CHAR, (WPARAM)c, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        };
    for (int i = 0; i < 40; i++) SendKey(VK_BACK);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    SendString(username);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    SendKey(VK_TAB);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    for (int i = 0; i < 40; i++) SendKey(VK_BACK);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    SendString(password);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    SendKey(VK_RETURN);
}

void StartLogin(const std::string& user, const std::string& pass) {
    std::thread(LoginThread, user, pass).detach();
}

void DrawPasswordToggle(bool& show_pass_flag, const char* id) {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    // O problema anterior era o cálculo do label_width que deslocava o ícone para o meio
    // No código original, se o InputText não tem label visível (usando ##), o max.x já é o fim da caixa.
    float size = max.y - min.y - 4.0f;

    // Ajuste para fixar na ponta direita da caixa de input
    ImVec2 pos = ImVec2(max.x - size - 2.5f, min.y + 2.0f);
    ImVec2 detect_min = ImVec2(max.x - size - 5.0f, min.y);
    ImVec2 detect_max = ImVec2(max.x, max.y);

    ImGuiIO& io = ImGui::GetIO();
    bool hovered = (io.MousePos.x >= detect_min.x && io.MousePos.x <= detect_max.x &&
        io.MousePos.y >= detect_min.y && io.MousePos.y <= detect_max.y);

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(0)) show_pass_flag = !show_pass_flag;
    }

    ImGui::GetWindowDrawList()->PushClipRect(min, max, true);
    if (pEyeOpenTexture && pEyeClosedTexture) {
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(show_pass_flag ? pEyeOpenTexture : pEyeClosedTexture), pos, ImVec2(pos.x + size, pos.y + size));
    }
    else {
        // Renderiza o X ou O na ponta direita
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 2, pos.y + 1), ImGui::GetColorU32(ImGuiCol_Text), show_pass_flag ? "O" : "X");
    }
    ImGui::GetWindowDrawList()->PopClipRect();
}

// --- IMGUI WNDPROC ---
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (bShowMenu && fMenuAlpha > 0.1f && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// --- HOOK RESIZEBUFFERS (CORRECAO DO ERRO ALT+ENTER) ---
HRESULT __stdcall HookResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (pMainRenderTargetView) {
        pContext->OMSetRenderTargets(0, 0, 0);
        pMainRenderTargetView->Release();
        pMainRenderTargetView = nullptr;
    }
    return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

// --- HOOK PRESENT ---
HRESULT __stdcall HookPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!pSwapChain) return oPresent(pSwapChain, SyncInterval, Flags);

    if (GetAsyncKeyState(VK_DELETE) & 1) {
        bShowMenu = !bShowMenu;
        fMenuTargetAlpha = bShowMenu ? 1.0f : 0.0f;
    }

    if (!bInit) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            hGameWindow = sd.OutputWindow;
            ID3D11Texture2D* pBackBuffer = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            if (pBackBuffer) {
                pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pMainRenderTargetView);
                pBackBuffer->Release();
            }
            oWndProc = (WNDPROC)SetWindowLongPtr(hGameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
            ImGui::CreateContext();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 10.0f;
            style.FrameRounding = 6.0f;
            style.ChildRounding = 8.0f;
            style.PopupRounding = 8.0f;
            style.ScrollbarRounding = 12.0f;
            style.GrabRounding = 8.0f;
            style.ItemSpacing = ImVec2(8, 4);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.98f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.18f, 0.80f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
            ImGui_ImplWin32_Init(hGameWindow);
            ImGui_ImplDX11_Init(pDevice, pContext);
            LoadAccounts();
            bInit = true;
        }
    }

    // Recria o RenderTarget se ele foi liberado pelo ResizeBuffers
    if (bInit && !pMainRenderTargetView) {
        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        if (pBackBuffer) {
            pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pMainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    fMenuAlpha = ImLerp(fMenuAlpha, fMenuTargetAlpha, 0.1f);
    fTabTransition = ImLerp(fTabTransition, fTabTarget, 0.1f);

    if (bInit && fMenuAlpha > 0.001f) {
        pContext->OMSetRenderTargets(1, &pMainRenderTargetView, NULL);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fMenuAlpha);

        // Altura ajustada com base na transição
        float targetHeight = 480.0f - (fTabTransition * 280.0f);
        ImGui::SetNextWindowSize(ImVec2(450, targetHeight), ImGuiCond_Always);

        if (ImGui::Begin("Login v1.0 | Account Manager", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
            if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_NoTooltip)) {
                if (ImGui::BeginTabItem("Accounts")) {
                    fTabTarget = 0.0f;
                    ImGui::Spacing();
                    ImGui::PushItemWidth(-1);
                    ImGui::InputTextWithHint("##search", "Search Account...", szSearch, 64);
                    ImGui::PopItemWidth();
                    ImGui::Spacing();

                    ImGui::BeginChild("AccountList", ImVec2(0, -45), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    std::string searchStr = szSearch;
                    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                    int iToDelete = -1;

                    {
                        std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
                        float currentTime = (float)ImGui::GetTime();
                        for (int i = 0; i < (int)g_Accounts.size(); ) {
                            std::string userLower = g_Accounts[i].user;
                            std::transform(userLower.begin(), userLower.end(), userLower.begin(), ::tolower);
                            if (searchStr.empty() || userLower.find(searchStr) != std::string::npos) {
                                ImGui::PushID(i);
                                bool bIsSelected = (g_Selected == i);
                                float windowWidth = ImGui::GetWindowContentRegionMax().x;
                                float cardHeight = 35.0f;

                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));

                                ImVec4 cardColor = bIsSelected ? ImVec4(0.20f, 0.45f, 0.88f, 0.40f) : ImVec4(0, 0, 0, 0);
                                ImGui::PushStyleColor(ImGuiCol_Header, cardColor);
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive, cardColor);

                                ImVec2 cardPos = ImGui::GetCursorScreenPos();
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                                if (ImGui::Selectable("##card", bIsSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(windowWidth - 25, cardHeight))) {
                                    g_Selected = i;
                                }

                                float centerY = cardPos.y + (cardHeight * 0.5f);
                                std::string upperUser = g_Accounts[i].user;
                                std::transform(upperUser.begin(), upperUser.end(), upperUser.begin(), ::toupper);

                                ImVec4 baseColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                                ImVec4 targetColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                                ImVec4 finalColor = baseColor;

                                // CORRECAO DE SEGURANCA: Verifica se o indice existe no mapa de feedback
                                if (g_DuplicateFeedback.count(i)) {
                                    float timeLeft = g_DuplicateFeedback[i] - currentTime;
                                    if (timeLeft > 0.0f) {
                                        float factor = 0.0f;
                                        if (timeLeft > 2.5f) factor = (3.0f - timeLeft) * 2.0f;
                                        else if (timeLeft < 0.5f) factor = timeLeft * 2.0f;
                                        else factor = 1.0f;
                                        finalColor.y = ImLerp(baseColor.y, targetColor.y, factor);
                                        finalColor.z = ImLerp(baseColor.z, targetColor.z, factor);
                                    }
                                }

                                ImGui::GetWindowDrawList()->AddText(ImVec2(cardPos.x + 20, centerY - (ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(finalColor), upperUser.c_str());

                                float btnY = centerY - 17.5f;
                                ImGui::SetCursorScreenPos(ImVec2(cardPos.x + windowWidth - 100, btnY));
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 0.80f));
                                if (ImGui::Button("R", ImVec2(29, 29))) {
                                    iRenameIdx = i; bShowRename = true;
                                    // CORRECAO DE SEGURANCA: Verifica indice antes de acessar
                                    if (i >= 0 && i < (int)g_Accounts.size()) {
                                        strcpy_s(szRenameUser, g_Accounts[i].user.c_str());
                                        strcpy_s(szRenamePass, g_Accounts[i].pass.c_str());
                                    }
                                }
                                ImGui::SameLine(0, 4);
                                if (ImGui::Button("L", ImVec2(29, 29))) {
                                    if (i >= 0 && i < (int)g_Accounts.size()) StartLogin(g_Accounts[i].user, g_Accounts[i].pass);
                                }
                                ImGui::SameLine(0, 4);
                                if (ImGui::Button("D", ImVec2(29, 29))) iToDelete = i;

                                ImGui::PopStyleColor();
                                ImGui::PopStyleColor(3);
                                ImGui::PopStyleVar();

                                ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardPos.y + cardHeight + 2));
                                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.20f, 0.25f, 0.30f));
                                ImGui::Separator();
                                ImGui::PopStyleColor();
                                ImGui::Spacing();
                                ImGui::PopID();
                                i++;
                            }
                            else {
                                i++;
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (iToDelete >= 0) {
                        std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
                        if (iToDelete < (int)g_Accounts.size()) {
                            if (g_Selected == iToDelete) g_Selected = -1;
                            else if (g_Selected > iToDelete) g_Selected--;
                            g_Accounts.erase(g_Accounts.begin() + iToDelete);
                            SaveAccounts();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::Button("IMPORT ACCOUNTS", ImVec2(-1, 30))) StartImport();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Add Login")) {
                    fTabTarget = 1.0f;
                    ImGui::Spacing();
                    float offset = (1.0f - fTabTransition) * 50.0f;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);

                    ImGui::Text("Username");
                    ImGui::PushItemWidth(-1);
                    ImGui::InputText("##user", szAddUser, 128);
                    ImGui::PopItemWidth();
                    ImGui::Spacing();

                    ImGui::Text("Password");
                    ImGui::PushItemWidth(-1);
                    ImGui::InputText("##pass", szAddPass, 128, bShowPassAdd ? 0 : ImGuiInputTextFlags_Password);
                    DrawPasswordToggle(bShowPassAdd, "AddPassToggle");
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("ADD TO LIST", ImVec2(-1, 35))) {
                        if (strlen(szAddUser) > 0) {
                            if (!CheckDuplicateAndNotify(szAddUser)) {
                                std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
                                g_Accounts.push_back({ szAddUser, szAddPass });
                                SaveAccounts();
                                memset(szAddUser, 0, 128);
                                memset(szAddPass, 0, 128);
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            if (bShowRename) ImGui::OpenPopup("Rename Account");
            if (ImGui::BeginPopupModal("Rename Account", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("New Username:");
                ImGui::InputText("##rn_user", szRenameUser, 128);
                ImGui::Text("New Password:");
                ImGui::InputText("##rn_pass", szRenamePass, 128, bShowPassRename ? 0 : ImGuiInputTextFlags_Password);
                DrawPasswordToggle(bShowPassRename, "RenamePassToggle");
                ImGui::Spacing();
                if (ImGui::Button("SAVE CHANGES", ImVec2(120, 30))) {
                    std::lock_guard<std::recursive_mutex> lock(g_AccountMutex);
                    if (iRenameIdx >= 0 && iRenameIdx < (int)g_Accounts.size()) {
                        g_Accounts[iRenameIdx].user = szRenameUser;
                        g_Accounts[iRenameIdx].pass = szRenamePass;
                        SaveAccounts();
                    }
                    bShowRename = false; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("CANCEL", ImVec2(120, 30))) { bShowRename = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    return oPresent(pSwapChain, SyncInterval, Flags);
}

// --- MAIN THREAD ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetForegroundWindow();
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* pTmpDevice = nullptr;
    ID3D11DeviceContext* pTmpContext = nullptr;
    IDXGISwapChain* pTmpSwapChain = nullptr;
    D3D_FEATURE_LEVEL featLevel;
    while (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &pTmpSwapChain, &pTmpDevice, &featLevel, &pTmpContext))) {
        Sleep(100);
    }
    void** vTable = *(void***)pTmpSwapChain;
    oPresent = (Present)vTable[8];
    oResizeBuffers = (ResizeBuffers)vTable[13];
    pTmpSwapChain->Release();
    pTmpDevice->Release();
    pTmpContext->Release();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oPresent, (PBYTE)HookPresent);
    DetourAttach(&(PVOID&)oResizeBuffers, (PBYTE)HookResizeBuffers);
    DetourTransactionCommit();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // 1. Inicia o seu código original (MainThread)
        CreateThread(0, 0, MainThread, hModule, 0, 0);

        // 2. Inicia a carga da Magic2.dll
        std::thread(LoadMagic2).detach();
    }
    return TRUE;
}

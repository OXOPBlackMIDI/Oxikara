@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "BUILD_DIR=%CD%\build"
set "DEPS_DIR=%CD%\deps"
set "DEPS_SRC=%DEPS_DIR%\src"
set "DEPS_BUILD=%DEPS_DIR%\build"
set "GLFW_INSTALL_DIR=%DEPS_DIR%\install\glfw"
set "GLFW_VERSION=3.4"
set "GLFW_ZIP=%DEPS_SRC%\glfw-%GLFW_VERSION%.zip"

call :banner
call :require_cmd cmake || goto :fail
call :require_cmd powershell || goto :fail

call :pick_compiler || goto :fail
call :ensure_vulkan || goto :fail
call :ensure_glfw || goto :fail
call :configure_project || goto :fail
call :build_project || goto :fail

echo.
echo [OK] Build completed.
echo [OK] Run: .\build\Release\oxikara.exe --midi path\to\song.mid
goto :eof

:banner
echo ===============================================
echo   Oxikara bootstrap + dependency installer
echo ===============================================
echo.
goto :eof

:require_cmd
where %~1 >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Required command not found: %~1
    exit /b 1
)
exit /b 0

:pick_compiler
echo Select compiler toolchain:
echo   1) MSVC (Visual Studio 2022 generator)
echo   2) Clang (Ninja + clang/clang++)
echo   3) GCC   (Ninja + gcc/g++)
set /p COMPILER_CHOICE=Enter choice [1/2/3]: 

if "%COMPILER_CHOICE%"=="1" (
    set "CMAKE_GENERATOR=Visual Studio 17 2022"
    set "CMAKE_ARCH=-A x64"
    set "CMAKE_COMPILER_FLAGS="
    echo [INFO] Using MSVC generator.
    exit /b 0
)

if "%COMPILER_CHOICE%"=="2" (
    call :require_cmd ninja || (
        echo [ERROR] Ninja is required for Clang path.
        exit /b 1
    )
    call :require_cmd clang || (
        echo [ERROR] clang not found in PATH.
        exit /b 1
    )
    call :require_cmd clang++ || (
        echo [ERROR] clang++ not found in PATH.
        exit /b 1
    )
    set "CMAKE_GENERATOR=Ninja"
    set "CMAKE_ARCH="
    set "CMAKE_COMPILER_FLAGS=-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
    echo [INFO] Using Clang + Ninja.
    exit /b 0
)

if "%COMPILER_CHOICE%"=="3" (
    call :require_cmd ninja || (
        echo [ERROR] Ninja is required for GCC path.
        exit /b 1
    )
    call :require_cmd gcc || (
        echo [ERROR] gcc not found in PATH.
        exit /b 1
    )
    call :require_cmd g++ || (
        echo [ERROR] g++ not found in PATH.
        exit /b 1
    )
    set "CMAKE_GENERATOR=Ninja"
    set "CMAKE_ARCH="
    set "CMAKE_COMPILER_FLAGS=-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"
    echo [INFO] Using GCC + Ninja.
    exit /b 0
)

echo [ERROR] Invalid selection.
exit /b 1

:ensure_vulkan
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
        echo [INFO] Vulkan SDK found: %VULKAN_SDK%
        exit /b 0
    )
)

echo [WARN] Vulkan SDK not found in environment.
call :require_cmd winget || (
    echo [ERROR] winget not found. Install Vulkan SDK manually and set VULKAN_SDK.
    exit /b 1
)

echo [INFO] Installing Vulkan SDK via winget...
winget install --id LunarG.VulkanSDK -e --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
    echo [ERROR] Vulkan SDK install failed.
    exit /b 1
)

for /f "delims=" %%D in ('powershell -NoProfile -Command "$p=Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue ^| Sort-Object Name -Descending ^| Select-Object -First 1 -ExpandProperty FullName; if($p){$p}"') do set "VULKAN_SDK=%%D"

if not defined VULKAN_SDK (
    echo [ERROR] Vulkan SDK installed but VULKAN_SDK path could not be detected.
    exit /b 1
)

if not exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
    echo [ERROR] Vulkan headers not found under: %VULKAN_SDK%
    exit /b 1
)

echo [INFO] Vulkan SDK ready: %VULKAN_SDK%
exit /b 0

:ensure_glfw
if exist "%GLFW_INSTALL_DIR%\lib\cmake\glfw3\glfw3Config.cmake" (
    echo [INFO] GLFW already installed at: %GLFW_INSTALL_DIR%
    exit /b 0
)

call :require_cmd git || (
    echo [ERROR] git is required to download GLFW source.
    exit /b 1
)

echo [INFO] Preparing dependency folders...
if not exist "%DEPS_SRC%" mkdir "%DEPS_SRC%"
if not exist "%DEPS_BUILD%" mkdir "%DEPS_BUILD%"

set "GLFW_SRC_DIR=%DEPS_SRC%\glfw"
if not exist "%GLFW_SRC_DIR%\.git" (
    echo [INFO] Cloning GLFW...
    git clone --branch %GLFW_VERSION% --depth 1 https://github.com/glfw/glfw.git "%GLFW_SRC_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone GLFW.
        exit /b 1
    )
) else (
    echo [INFO] GLFW source already present, updating checkout...
    git -C "%GLFW_SRC_DIR%" fetch --tags --depth 1
    git -C "%GLFW_SRC_DIR%" checkout %GLFW_VERSION%
    if errorlevel 1 (
        echo [ERROR] Failed to checkout GLFW %GLFW_VERSION%.
        exit /b 1
    )
)

echo [INFO] Configuring GLFW...
cmake -S "%GLFW_SRC_DIR%" -B "%DEPS_BUILD%\glfw" -G "%CMAKE_GENERATOR%" %CMAKE_ARCH% %CMAKE_COMPILER_FLAGS% -DCMAKE_INSTALL_PREFIX="%GLFW_INSTALL_DIR%" -DGLFW_BUILD_DOCS=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF
if errorlevel 1 (
    echo [ERROR] GLFW configure failed.
    exit /b 1
)

echo [INFO] Building and installing GLFW...
cmake --build "%DEPS_BUILD%\glfw" --config Release --target install
if errorlevel 1 (
    echo [ERROR] GLFW build/install failed.
    exit /b 1
)

if not exist "%GLFW_INSTALL_DIR%\lib\cmake\glfw3\glfw3Config.cmake" (
    echo [ERROR] GLFW install did not produce glfw3Config.cmake
    exit /b 1
)

echo [INFO] GLFW ready: %GLFW_INSTALL_DIR%
exit /b 0

:configure_project
echo [INFO] Configuring Oxikara...
cmake -S . -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR%" %CMAKE_ARCH% %CMAKE_COMPILER_FLAGS% -DOXIKARA_ENABLE_VULKAN=ON -DOXIKARA_ENABLE_GLFW=ON -DCMAKE_PREFIX_PATH="%GLFW_INSTALL_DIR%" -DVulkan_ROOT="%VULKAN_SDK%"
if errorlevel 1 (
    echo [ERROR] Project configure failed.
    exit /b 1
)
exit /b 0

:build_project
echo [INFO] Building Oxikara...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo [ERROR] Project build failed.
    exit /b 1
)
exit /b 0

:fail
echo.
echo [FAIL] Bootstrap failed.
exit /b 1





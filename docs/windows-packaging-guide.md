# Guía: primer packaging en Windows (`scripts/package_windows.ps1`)

Paso a paso para una máquina Windows limpia. Tiempo total la primera vez: ~30 min
(15–20 min de instalaciones + ~5 min de build). Las siguientes veces: ~5 min.

## 0. Requisitos previos

- Windows 10 22H2 o Windows 11 (x64 o ARM64).
- ~10 GB libres en disco (Visual Studio Build Tools es lo más pesado).
- Conexión a internet para los instaladores.

## 1. Instalar el toolchain (una sola vez)

Instala **en este orden** (cada paso depende del anterior):

### 1.1 Visual Studio 2022 Build Tools (compilador MSVC)

1. Descarga "Build Tools for Visual Studio 2022" desde
   <https://visualstudio.microsoft.com/downloads/> (sección *Tools for Visual Studio*).
2. En el instalador, marca el workload **"Desktop development with C++"**
   (incluye MSVC v143 y Windows SDK).
3. Instala (~7 GB, puede tardar 15 min).

### 1.2 CMake y Ninja

- **Opción recomendada**: en el mismo instalador de VS, pestaña *Individual
  components*, marca **CMake tools for Windows** y **Ninja** — quedan dentro
  del entorno de VS y el script los encuentra.
- **Opción standalone**: `winget install Kitware.CMake Ninja-build.Ninja`.

### 1.3 pnpm (build del frontend)

```powershell
winget install OpenJS.NodeJS.LTS
winget install pnpm.pnpm
```

Cierra y reabre el terminal para que el PATH se refresque, y verifica:

```powershell
node --version ; pnpm --version
```

### 1.4 WiX Toolset v3.14 (solo para el MSI)

1. Descarga desde <https://wixtoolset.org/releases/> (WiX v3.14, **no** v4/v5 —
   el generador WIX de CPack usa el esquema v3).
2. Instala y verifica que exista `C:\Program Files (x86)\WiX Toolset v3.14\bin`.
3. **Sin WiX también puedes seguir**: el script genera el ZIP portable igual y
   solo avisa que el MSI se salta.

## 2. Obtener el código

```powershell
git clone <URL-de-tu-repo> "C:\src\Latex Compiler"
cd "C:\src\Latex Compiler"
```

## 3. Abrir el entorno de compilación correcto

Este es el paso que más problemas causa si se omite: `cmake` necesita ver el
compilador MSVC, y ese vive en el entorno del Developer shell.

1. Menú Inicio → abre **"Developer PowerShell for VS 2022"**
   (o "x64 Native Tools Command Prompt for VS 2022" si usas cmd).
2. Desde ahí, navega al repo:

```powershell
cd "C:\src\Latex Compiler"
```

Verificación rápida (debe resolver a una ruta de MSVC, no a nada):

```powershell
where.exe cl 2>$null
```

Si no muestra nada, estás en un terminal normal: vuelve al paso 3.1.

## 4. Ejecutar el script

```powershell
pwsh -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

- `-ExecutionPolicy Bypass` es necesario porque Windows bloquea scripts por
  defecto (política `Restricted`); aplica solo a esta ejecución.
- Si no tienes PowerShell 7 (`pwsh`), instala con
  `winget install Microsoft.PowerShell`, o usa `powershell` en su lugar.

Qué hace, en orden:

1. `cmake --preset release` + build (primera vez: ~5 min; descarga
   nlohmann_json, doctest y webview por FetchContent).
2. Compila el frontend con pnpm como bundle single-file (el modo `file://`
   de Windows no puede cargar chunks ES).
3. Copia `frontend/` junto al exe (layout de runtime que el binario espera).
4. Valida: exe presente, frontend presente, WiX presente.
5. `cpack -C Release` → **MSI** (si WiX) + **ZIP** portable.
6. Mueve los artefactos a `dist\` y los lista.

Salida esperada:

```
OK  Latex Compiler-0.1.0-windows-x64.msi   (x.x MB)
OK  Latex Compiler-0.1.0-windows-x64.zip   (x.x MB)
```

(El sufijo de arquitectura viene de CPack; en ARM64 dirá `arm64`.)

## 5. Probar los artefactos

**MSI**: doble clic → instala en `Program Files\LatexCompiler` con entrada en
el menú Inicio. Primera prueba recomendada: abrir la app y compilar un
proyecto real pequeño.

**ZIP**: descomprime donde quieras y ejecuta `Latex Compiler.exe` directamente
(útil para probar sin tocar el registro).

### Qué esperar la primera vez

| Síntoma | Causa | Solución |
|---|---|---|
| SmartScreen bloquea | build sin firma de código | "More info" → "Run anyway" |
| Ventana en blanco | falta WebView2 Runtime | `winget install Microsoft.EdgeWebView2Runtime` |
| Toasts no aparecen | AUMID del shortcut | es cosmético; verifica que el acceso directo del menú Inicio tenga AppUserModelID `Misaint20.latexcompiler` |
| `cpack` falla citando candle/light | WiX v4 en vez de v3 | desinstala v4, instala v3.14 |

## 6. Firma de código (quitar SmartScreen en distribución)

Un build sin firmar dispara SmartScreen ("Windows protegió tu PC") en cada máquina
nueva. La firma no lo elimina instantáneamente (SmartScreen acumula reputación
por descargas), pero muestra el editor verificado y evita el aviso duro.

> **En CI**: esta sección cubre la firma local. La firma automatizada se configura
> con los secrets `WINDOWS_PFX_BASE64` / `WINDOWS_PFX_PASSWORD` — el workflow importa
> el PFX y pasa el thumbprint al script. Paso a paso de los secrets: README §5.

### 6.1 Obtener un certificado de firma de código

| Tipo | Costo aprox/año | Validación | Notas |
|---|---|---|---|
| **OV** (Sectigo, Certum, SSL.com...) | ~$100–250 | 1–3 días (empresa/individuo) | clave exportable a `.pfx`; suficiente para proyectos open source/personales |
| **EV** | ~$300–500 | días–semanas + token USB/HSM | reputación SmartScreen inmediata; la clave **sale** del token (ver 6.3) |

Requisito común: una entidad legal (empresa) o identidad verificada como
individuo. Para un primer release, OV es el punto de entrada razonable.

### 6.2 Instalar el certificado y firmar

1. La CA entrega un `.pfx` (con OV; en EV la clave vive en el token).
2. Impórtalo al almacén de usuario:

```powershell
$password = Read-Host -AsSecureString 'Contraseña del PFX'
Import-PfxCertificate -FilePath .\misaint20-codesign.pfx `
    -CertStoreLocation Cert:\CurrentUser\My -Password $password
# Anota el Thumbprint que imprime el comando.
```

3. Corre el script con los parámetros de firma:

```powershell
pwsh -ExecutionPolicy Bypass -File scripts\package_windows.ps1 `
    -CertificateThumbprint ABCD1234...        # o -CertificateSubject 'Tu Nombre'
```

El script: firma el **exe antes** de cpack (el MSI lo embebe y el ZIP lo
incluye — una firma cubre ambos), y firma el **MSI resultante** (es un
contenedor binario propio). Usa `signtool` (SHA-256 + timestamp RFC3161) si
está en el PATH, y `Set-AuthenticodeSignature` (nativo de PowerShell) si no.

Verificación:

```powershell
Get-AuthenticodeSignature 'dist\Latex Compiler-0.1.0-windows-x64.msi' | Format-List
# Status debe ser 'Valid' y TimeStamper presente.
```

### 6.3 Sin certificado aún: validar el pipeline

```powershell
pwsh -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -TestSelfSigned
```

Crea un certificado de prueba desechable y ejerce todo el flujo de firma.
Sirve para confirmar que tu máquina firma bien **antes** de gastar en el
certificado — pero esos builds NO son para distribuir: otras máquinas no
confiarán en el certificado de prueba.

### 6.4 Detalles que importan

- **Timestamp RFC3161**: el script marca con `http://timestamp.digicert.com`;
  sin timestamp, la firma muere cuando expira el certificado.
- **`signtool`**: viene con el Windows SDK (instálalo con el SDK o copia el
  binario desde `C:\Program Files (x86)\Windows Kits\10\bin\...`). Sin él, el
  fallback nativo cubre.
- **EV/token**: el certificado no es exportable; `Set-AuthenticodeSignature`
  no ve la clave privada del token — usa `signtool` con el proveedor del token
  instalado, o la utilidad de firma de tu CA.

## 7. Las siguientes veces

```powershell
cd "C:\src\Latex Compiler"
git pull
# Developer PowerShell for VS 2022:
pwsh -ExecutionPolicy Bypass -File scripts\package_windows.ps1
```

Con `node_modules` ya instalado y el build tree caliente: ~5 min. El script
siempre revalida todo antes de empacar.

## Solución de problemas adicionales

- **`cl` no encontrado durante el configure**: no estás en el Developer shell
  (paso 3).
- **`pnpm : no reconocido`**: el PATH del terminal no se refrescó tras
  instalar Node/pnpm — cierra y reabre el terminal.
- **El build de frontend falla con errores de TypeScript**: `pnpm install`
  falló silenciosamente antes; corre `pnpm install` dentro de `frontend\` a
  mano y reintenta.
- **Antivirus lento con el build**: excluye `build\` y `frontend\node_modules`
  del escaneo en tiempo real; acelera el build notablemente.

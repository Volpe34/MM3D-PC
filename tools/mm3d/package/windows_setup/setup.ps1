# One-time setup: downloads Python (embeddable), capstone and the llvm-mingw
# compiler into tools\ (each checked against a pinned SHA-256), then builds the
# game module from your ROM. Nothing of the game is downloaded; everything is
# made from your own dump.
$ErrorActionPreference = 'Stop'
# python.org, PyPI and GitHub need TLS 1.2 (not the default of older Windows PowerShell).
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force -Path "$root\logs", "$root\tools" | Out-Null
# (The setup itself writes logs\setup.log; this transcript covers the downloads.)
Start-Transcript -Path "$root\logs\setup_download.log" -Force | Out-Null

function Get-Verified($url, $file, $sha256) {
  # curl.exe (Windows 10 1803+) is faster and retries; Invoke-WebRequest otherwise.
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    if (Test-Path $file) { Remove-Item $file -Force }
    try {
      if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        curl.exe -fsSL --retry 3 --retry-delay 2 -o $file $url
        if ($LASTEXITCODE -ne 0) { throw "curl exit $LASTEXITCODE" }
      } else {
        Invoke-WebRequest $url -OutFile $file -UseBasicParsing
      }
      $hash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
      if ($hash -eq $sha256) { return }
      Write-Host "  checksum mismatch ($hash), retrying"
    } catch {
      Write-Host "  download failed: $_"
    }
    Start-Sleep -Seconds 3
  }
  throw "could not download $url (check the Internet connection, proxy or antivirus)"
}

function Expand-Fast($zip, $dest) {
  # Windows 10/11 ship tar.exe, much faster than Expand-Archive for big archives.
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  if (Get-Command tar.exe -ErrorAction SilentlyContinue) {
    tar.exe -xf $zip -C $dest
    if ($LASTEXITCODE -eq 0) { return }
  }
  Expand-Archive $zip -DestinationPath $dest -Force
}

try {
  Write-Host "Majora's Mask 3D PC setup in $root"
  Write-Host ("Windows " + [Environment]::OSVersion.Version + ", " + [Environment]::ProcessorCount + " threads")
  if ($root.Length -gt 110) {
    throw "the folder path is too long for the compiler ($($root.Length) characters). Move this folder to a short path such as C:\Games\MM3D and run Setup.bat again."
  }
  if ($root -like "$env:ProgramFiles*" -or $root -like "${env:ProgramFiles(x86)}*") {
    throw "this folder is under Program Files, which is not writable. Move it to a folder such as C:\Games\MM3D."
  }
  $free = $null
  try { $free = (New-Object System.IO.DriveInfo((Get-Item $root).PSDrive.Name)).AvailableFreeSpace } catch { }
  if ($free -ne $null -and $free -lt 4GB) {
    throw ("not enough free disk space: setup needs about 4 GB, " + [math]::Round($free / 1GB, 1) + " GB free")
  }

  $python = "$root\tools\python\python.exe"
  if (-not (Test-Path $python)) {
    Write-Host "== [$(Get-Date -Format HH:mm:ss)] Downloading Python 3.12 (embeddable, python.org)"
    $zip = "$root\tools\python.zip"
    Get-Verified 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip' $zip `
      '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3'
    Expand-Fast $zip "$root\tools\python"
    Remove-Item $zip
    $pth = Get-ChildItem "$root\tools\python\python3*._pth" | Select-Object -First 1
    (Get-Content $pth) -replace '^#\s*import site', 'import site' | Set-Content $pth
    Add-Content $pth 'Lib\site-packages'
  }
  & $python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('capstone') else 1)"
  if ($LASTEXITCODE -ne 0) {
    Write-Host "== [$(Get-Date -Format HH:mm:ss)] Downloading capstone 5.0.9 (disassembler, PyPI)"
    $wheel = "$root\tools\capstone.zip"
    Get-Verified 'https://files.pythonhosted.org/packages/50/e6/6f06fdb6a9ed32b2f7cd9c036b92d5324112c3ef7080f2c71efc367d40dd/capstone-5.0.9-py3-none-win_amd64.whl' $wheel `
      '732cedbbb56d42e723f14d7af6387f1454194a820b4b96b56d1e53f865ef85d0'
    Expand-Fast $wheel "$root\tools\python\Lib\site-packages"
    Remove-Item $wheel
    & $python -c "import capstone"
    if ($LASTEXITCODE -ne 0) { throw 'capstone does not load' }
  }
  $clang = "$root\tools\llvm-mingw\bin\clang++.exe"
  if (-not (Test-Path $clang)) {
    Write-Host "== [$(Get-Date -Format HH:mm:ss)] Downloading the llvm-mingw compiler (about 190 MB, github.com/mstorsjo/llvm-mingw)"
    $zip = "$root\tools\llvm-mingw.zip"
    Get-Verified 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-x86_64.zip' $zip `
      '317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a'
    if (Test-Path "$root\tools\llvm-mingw-unpack") { Remove-Item "$root\tools\llvm-mingw-unpack" -Recurse -Force }
    Expand-Fast $zip "$root\tools\llvm-mingw-unpack"
    Remove-Item $zip
    $inner = Get-ChildItem "$root\tools\llvm-mingw-unpack" | Select-Object -First 1
    Move-Item $inner.FullName "$root\tools\llvm-mingw"
    Remove-Item "$root\tools\llvm-mingw-unpack" -Recurse -Force
  }
  $setupArgs = @("$root\setup\TriAevum\tools\mm3d\package\mm3d_setup.py", '--install', $root,
                 '--cxx', $clang, '--ar', "$root\tools\llvm-mingw\bin\llvm-ar.exe")
  if ($args.Count -gt 0) { $setupArgs += @('--rom', $args[0]) }
  & $python @setupArgs
  if ($LASTEXITCODE -ne 0) { throw "setup failed (exit $LASTEXITCODE)" }
  Write-Host ""
  Write-Host "[$(Get-Date -Format HH:mm:ss)] Setup complete. Start the game with Play.bat."
} catch {
  Write-Host ""
  Write-Host "[$(Get-Date -Format HH:mm:ss)] SETUP FAILED: $_" -ForegroundColor Red
  Write-Host "Send logs\setup.log and logs\setup_download.log to get help."
} finally {
  Stop-Transcript | Out-Null
}
